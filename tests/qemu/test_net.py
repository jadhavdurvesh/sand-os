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
        for name in ("FILES", "IPC", "NOTIFY", "NET"):
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
