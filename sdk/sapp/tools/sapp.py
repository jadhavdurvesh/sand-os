#!/usr/bin/env python3
"""SAPP v0.2 packer and strict host-side validator."""
import argparse
import json
import pathlib
import struct
import sys

MAGIC=b"SAPP"
VERSION=1
ARCH_X86_32=1
HEADER_SIZE=64
SEGMENT_SIZE=32
RELOC_SIZE=12
PERMS={"r":1,"w":2,"x":4}
HEADER=struct.Struct("<4sHHIIIIIIIIIIII8s")
SEGMENT=struct.Struct("<IIIIII8s")
RELOC=struct.Struct("<III")
ELF_HEADER=struct.Struct("<16sHHIIIIIHHHHHH")
ELF_PROGRAM=struct.Struct("<IIIIIIII")
PT_LOAD=1
PF_X,PF_W,PF_R=1,2,4
R_SAND_ABS32=1


def fail(message):
    raise ValueError(message)


def read_manifest(path):
    data=json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    for key in ("id","name","version"):
        if not isinstance(data.get(key),str) or not data[key].strip():
            fail(f"manifest requires non-empty string '{key}'")
    app_id=data["id"]
    allowed=set("abcdefghijklmnopqrstuvwxyz0123456789.-")
    if not app_id or any(ch not in allowed for ch in app_id) or app_id[0] in ".-" or app_id[-1] in ".-":
        fail("manifest id must use lowercase ASCII letters, digits, dots and hyphens")
    return data


def read_elf32(path):
    blob=pathlib.Path(path).read_bytes()
    if len(blob)<ELF_HEADER.size:
        fail("input is not a complete ELF32 file")
    h=ELF_HEADER.unpack_from(blob)
    ident,etype,machine,version,entry,phoff,_,_,ehsize,phentsize,phnum,*_=h
    if ident[:7]!=b"\x7fELF\x01\x01\x01" or etype!=2 or machine!=3 or version!=1:
        fail("input must be a little-endian, static ELF32 i386 executable")
    if ehsize!=ELF_HEADER.size or phentsize!=ELF_PROGRAM.size or phnum==0 or phnum>8:
        fail("unsupported ELF32 program header layout")
    if phoff>len(blob) or phnum>(len(blob)-phoff)//phentsize:
        fail("ELF program header table is out of bounds")
    segments=[]
    for i in range(phnum):
        typ,off,vaddr,_,filesz,memsz,flags,align=ELF_PROGRAM.unpack_from(blob,phoff+i*phentsize)
        if typ!=PT_LOAD:
            continue
        if memsz==0 or filesz>memsz or off>len(blob) or filesz>len(blob)-off:
            fail(f"ELF load segment {i} has invalid bounds")
        if vaddr%4096 or align<4096 or align&(align-1):
            fail(f"ELF load segment {i} must be page aligned")
        perm=(1 if flags&PF_R else 0)|(2 if flags&PF_W else 0)|(4 if flags&PF_X else 0)
        if not perm&1 or perm&6==6:
            fail(f"ELF load segment {i} has invalid permissions (W+X forbidden)")
        segments.append({"vaddr":vaddr,"memsz":memsz,"data":blob[off:off+filesz],
                         "filesz":filesz,"permissions":perm,"alignment":align})
    if not segments:
        fail("ELF contains no PT_LOAD segments")
    return entry,segments


def load_relocations(path):
    if not path:
        return []
    data=json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    if not isinstance(data,list) or len(data)>256:
        fail("relocation file must be a JSON array of at most 256 entries")
    result=[]
    for item in data:
        if not isinstance(item,dict) or not isinstance(item.get("target"),int) or not isinstance(item.get("addend"),int):
            fail("each relocation needs integer target and addend fields")
        if not 0<=item["target"]<=0xffffffff or not 0<=item["addend"]<=0xffffffff:
            fail("relocation values must fit uint32")
        result.append((item["target"],R_SAND_ABS32,item["addend"]))
    return result


def pack(args):
    manifest=read_manifest(args.manifest)
    entry,segments=read_elf32(args.input)
    relocs=load_relocations(args.relocations)
    for i,s in enumerate(segments):
        for other in segments[:i]:
            if s["vaddr"]<other["vaddr"]+other["memsz"] and other["vaddr"]<s["vaddr"]+s["memsz"]:
                fail("ELF load segments overlap in virtual memory")
    if not any((s["permissions"]&4) and s["vaddr"]<=entry<s["vaddr"]+s["memsz"] for s in segments):
        fail("ELF entry is not inside an executable load segment")
    for target,_,_ in relocs:
        if not any((s["permissions"]&2) and s["vaddr"]<=target<=s["vaddr"]+s["memsz"]-4 for s in segments):
            fail(f"relocation target {target:#x} is not in writable memory")
    table_off=HEADER_SIZE
    meta=json.dumps(manifest,sort_keys=True,separators=(",",":")).encode()
    meta_off=table_off+len(segments)*SEGMENT_SIZE
    reloc_off=meta_off+len(meta) if relocs else 0
    payload_off=(meta_off+len(meta)+len(relocs)*RELOC_SIZE+15)&~15
    payload=bytearray()
    records=[]
    cursor=payload_off
    for s in segments:
        records.append(SEGMENT.pack(s["vaddr"],s["memsz"],cursor,s["filesz"],
                                    s["permissions"],s["alignment"],bytes(8)))
        payload.extend(s["data"])
        cursor+=s["filesz"]
    file_size=cursor
    header=HEADER.pack(MAGIC,VERSION,ARCH_X86_32,HEADER_SIZE,0,entry,
                       table_off,len(segments),meta_off,len(meta),file_size,
                       0,0,reloc_off,len(relocs),bytes(8))
    out=bytearray(header+b"".join(records)+meta)
    if relocs:
        out.extend(b"".join(RELOC.pack(*r) for r in relocs))
    out.extend(bytes(payload_off-len(out)))
    out.extend(payload)
    pathlib.Path(args.output).write_bytes(out)
    print(f"created {args.output}: {len(segments)} segments, {len(relocs)} relocations, {file_size} bytes")


def parse(path):
    blob=pathlib.Path(path).read_bytes()
    if len(blob)<HEADER_SIZE:
        fail("file is shorter than SAPP header")
    (magic,version,arch,hsize,flags,entry,table_off,count,meta_off,meta_len,
     file_size,sig_off,sig_len,reloc_off,reloc_count,reserved)=HEADER.unpack_from(blob)
    if magic!=MAGIC or version!=VERSION or arch!=ARCH_X86_32:
        fail("bad magic, unsupported version or architecture")
    if hsize!=HEADER_SIZE or flags or reserved!=bytes(8) or file_size!=len(blob):
        fail("invalid header or declared file size")
    if not 1<=count<=8 or count*SEGMENT_SIZE>len(blob) or table_off<HEADER_SIZE or table_off+count*SEGMENT_SIZE>len(blob):
        fail("segment table out of bounds")
    if meta_off<table_off+count*SEGMENT_SIZE or meta_len==0 or meta_len>4096 or meta_off+meta_len>len(blob):
        fail("metadata range out of bounds")
    if sig_off or sig_len:
        fail("signature verification is not implemented; signed files rejected")
    if reloc_count>256 or (reloc_count and (not reloc_off or reloc_off<meta_off+meta_len or reloc_off+reloc_count*RELOC_SIZE>len(blob))):
        fail("relocation table out of bounds")
    metadata=json.loads(blob[meta_off:meta_off+meta_len].decode("utf-8"))
    if not isinstance(metadata,dict) or not all(isinstance(metadata.get(k),str) and metadata[k] for k in ("id","name","version")):
        fail("metadata requires id/name/version strings")
    segments=[]
    vranges=[]
    franges=[]
    entry_ok=False
    for i in range(count):
        vaddr,memsz,off,filesz,perm,align,zero=SEGMENT.unpack_from(blob,table_off+i*SEGMENT_SIZE)
        if zero!=bytes(8) or not memsz or filesz>memsz or off<meta_off+meta_len or off+filesz>len(blob):
            fail(f"segment {i}: invalid sizes or file range")
        if perm&~7 or not perm&1 or perm&6==6:
            fail(f"segment {i}: invalid permissions (W+X forbidden)")
        if align<4096 or align&(align-1) or vaddr%align or vaddr+memsz>0x100000000:
            fail(f"segment {i}: invalid alignment or address overflow")
        vranges.append((vaddr,vaddr+memsz))
        franges.append((off,off+filesz))
        if perm&4 and vaddr<=entry<vaddr+memsz:
            entry_ok=True
        segments.append({"vaddr":hex(vaddr),"memsz":memsz,"file_offset":off,
                         "filesz":filesz,"permissions":perm,"alignment":align})
    for ranges,label in ((vranges,"virtual"),(franges,"file")):
        ordered=sorted(ranges)
        if any(a[1]>b[0] for a,b in zip(ordered,ordered[1:])):
            fail(f"overlapping {label} segments")
    if not entry_ok:
        fail("entry point is not inside executable segment")
    relocs=[]
    for i in range(reloc_count):
        target,typ,addend=RELOC.unpack_from(blob,reloc_off+i*RELOC_SIZE)
        if typ!=R_SAND_ABS32 or (target&0xfff)>0xffc:
            fail(f"relocation {i}: unsupported type or page-crossing target")
        if not any((s["permissions"]&2) and int(s["vaddr"],16)<=target<=int(s["vaddr"],16)+s["memsz"]-4 for s in segments):
            fail(f"relocation {i}: target is not writable")
        relocs.append({"target":hex(target),"type":"SAND_ABS32","addend":addend})
    return {"format":"SAPP","version":version,"architecture":"x86-32","entry":hex(entry),
            "metadata":metadata,"segments":segments,"relocations":relocs,"file_size":file_size}


def main():
    parser=argparse.ArgumentParser(prog="sapp")
    sub=parser.add_subparsers(dest="command",required=True)
    p=sub.add_parser("pack",help="convert a static ELF32 i386 executable into SAPP")
    p.add_argument("--input",required=True); p.add_argument("--manifest",required=True)
    p.add_argument("--output",required=True); p.add_argument("--relocations")
    p.set_defaults(func=pack)
    for cmd in ("verify","info"):
        p=sub.add_parser(cmd); p.add_argument("file"); p.set_defaults(func=lambda a: print(json.dumps(parse(a.file),indent=2)))
    args=parser.parse_args()
    try: args.func(args)
    except (OSError,ValueError,json.JSONDecodeError,struct.error) as exc:
        print(f"sapp: error: {exc}",file=sys.stderr); return 2
    return 0


if __name__=="__main__":
    raise SystemExit(main())
