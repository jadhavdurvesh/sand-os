"""DRIFTNET / Veyra subsystem tests under QEMU (RTL8139 on QEMU's user network)."""
import socket, sys, threading, time, unittest
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from sand_qemu import SandVM

IMAGE = "build/sand-os.img"

def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

class NetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_udp_port()
        cls.vm = SandVM(IMAGE, nic=f"user,model=rtl8139,hostfwd=udp:127.0.0.1:{cls.port}-:7777").start()
        assert cls.vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40)
    @classmethod
    def tearDownClass(cls):
        cls.vm.stop()

    def test_1_veyra_discovery(self):
        out = self.vm.run_command("veyra")
        for name in ("FILES", "IPC", "NOTIFY", "NET", "HUGINN"):
            self.assertIn(name, out)
        self.assertNotIn("not present", out)           # the NIC exists in this VM
    def test_2_nic_is_up(self):
        self.assertIn("SAND_TEST:DRIFTNET_NIC_UP", self.vm.debug_log())
        out = self.vm.run_command("net")
        self.assertIn("10.0.2.15", out); self.assertIn("52:54:00", out)
    def test_3_ping_gateway(self):
        out = self.vm.run_command("ping 10.0.2.2", timeout=15)
        self.assertIn("reply from 10.0.2.2", out)
        self.assertIn("SAND_AUDIT:NET_PING 10.0.2.2", self.vm.debug_log())
    def test_4_ping_nobody_times_out(self):
        out = self.vm.run_command("ping 10.0.2.99", timeout=20)
        self.assertIn("timeout", out)
    def test_5_bad_ping_usage(self):
        self.assertIn("usage", self.vm.run_command("ping 10.0.2"))
    def test_6_udp_echo_through_the_host(self):
        got = {}
        def host():
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(25)
            for _ in range(40):                           # the guest binds the port once netecho runs
                s.sendto(b"driftnet says hi", ("127.0.0.1", self.port))
                try:
                    got["data"], _ = s.recvfrom(600); return
                except socket.timeout:
                    pass
        t = threading.Thread(target=host); t.start()
        out = self.vm.run_command("run netecho", timeout=40)
        t.join(30)
        self.assertIn("echoed 16 bytes", out)
        self.assertEqual(got.get("data"), b"driftnet says hi")
        self.assertIn("SAND_AUDIT:NET_UDP_SEND", self.vm.debug_log())

    def test_7_dhcp_bound_on_default_network(self):
        self.assertIn("SAND_TEST:DRIFTNET_DHCP_BOUND", self.vm.debug_log())
        self.assertIn("from DHCP", self.vm.run_command("net"))
    def test_8_tcp_round_trip_with_the_host(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 7778)); srv.listen(1); srv.settimeout(30)
        seen = {}
        def serve():
            try:
                c, _ = srv.accept(); c.settimeout(20)
                data = b""
                while not data.endswith(b"\n"): data += c.recv(100)
                seen["got"] = data; c.sendall(data.upper()); c.close()
            except Exception as e:
                seen["err"] = repr(e)
        t = threading.Thread(target=serve); t.start()
        out = self.vm.run_command("run netget", timeout=40)
        t.join(30); srv.close()
        self.assertIn("connected", out); self.assertIn("reply: HELLO TCP", out); self.assertIn("closed", out)
        self.assertEqual(seen.get("got"), b"hello tcp\n")
        self.assertIn("SAND_AUDIT:NET_TCP_CONNECT", self.vm.debug_log())
    def test_9_tcp_refused_is_reported(self):
        out = self.vm.run_command("run netget", timeout=40)             # nothing listens on 7778 now
        self.assertIn("connect failed", out)

    def test_a_huginn_resolves_names(self):
        """A tiny fake DNS server on the host (reached through the gateway) answers one name with a compressed A record."""
        srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); srv.bind(("127.0.0.1", 5353)); srv.settimeout(20)
        seen = []
        def serve():
            try:
                for _ in range(2):
                    q, addr = srv.recvfrom(512)
                    name = b""; i = 12
                    while q[i]: name += q[i + 1:i + 1 + q[i]] + b"."; i += 1 + q[i]
                    seen.append(name)
                    hdr = q[:2]
                    if name == b"odin.sand.":
                        resp = hdr + b"\x81\x80\x00\x01\x00\x02\x00\x00\x00\x00" + q[12:i + 5]
                        resp += b"\xc0\x0c\x00\x05\x00\x01\x00\x00\x00\x3c\x00\x02\xc0\x0c"        # a CNAME first: must be skipped
                        resp += b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x01\x2c\x00\x04\x0a\x09\x08\x07"     # then the A record 10.9.8.7, ttl 300
                    else:
                        resp = hdr + b"\x81\x83\x00\x01\x00\x00\x00\x00\x00\x00" + q[12:i + 5]          # NXDOMAIN
                    srv.sendto(resp, addr)
            except Exception as e:
                seen.append(repr(e).encode())
        t = threading.Thread(target=serve); t.start()
        out = self.vm.run_command("huginn odin.sand 10.0.2.2:5353", timeout=20)
        self.assertIn("odin.sand is 10.9.8.7", out)
        out = self.vm.run_command("huginn nowhere.sand 10.0.2.2:5353", timeout=20)
        self.assertIn("no such name", out)
        t.join(25); srv.close()
        self.assertEqual(seen[:2], [b"odin.sand.", b"nowhere.sand."])
        self.assertIn("SAND_AUDIT:NET_RESOLVE odin.sand 10.9.8.7", self.vm.debug_log())
        self.assertIn("(cached)", self.vm.run_command("huginn odin.sand", timeout=10))      # second lookup needs no server
    def test_b_huginn_rejects_bad_input(self):
        self.assertIn("usage", self.vm.run_command("huginn"))
        self.assertIn("bad name", self.vm.run_command("huginn BAD..name", timeout=10))
        self.assertIn("bad server", self.vm.run_command("huginn a.b 10.0.2", timeout=10))
    def test_c_huginn_times_out_without_an_answer(self):
        self.assertIn("timeout", self.vm.run_command("huginn silent.sand 10.0.2.2:5399", timeout=20))

class CustomSubnetTest(unittest.TestCase):
    def test_dhcp_hands_out_a_non_default_address(self):
        vm = SandVM(IMAGE, nic="user,model=rtl8139,net=192.168.76.0/24,dhcpstart=192.168.76.50").start()
        try:
            self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40))
            self.assertIn("SAND_TEST:DRIFTNET_DHCP_BOUND", vm.debug_log())
            out = vm.run_command("net")
            self.assertIn("192.168.76.50", out); self.assertIn("192.168.76.2", out); self.assertIn("255.255.255.0", out)
            self.assertIn("reply from 192.168.76.2", vm.run_command("ping 192.168.76.2", timeout=15))
        finally:
            vm.stop()

class NoNicTest(unittest.TestCase):
    def test_absent_nic_is_reported_not_fatal(self):
        vm = SandVM(IMAGE).start()
        try:
            self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40))
            self.assertIn("not present", vm.run_command("veyra"))
            self.assertIn("no network card", vm.run_command("net"))
            self.assertIn("no network card", vm.run_command("ping 10.0.2.2"))
        finally:
            vm.stop()

if __name__ == "__main__":
    unittest.main()
