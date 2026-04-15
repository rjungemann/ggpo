#include <iostream>

#include "types.h"
#include "network/udp_msg.h"
#include "network/udp_proto.h"
#include "game_input.h"

namespace {

class TestUdpProtocol : public UdpProtocol {
public:
   using UdpProtocol::OnInputAck;

   void SetActive() { _udp = reinterpret_cast<Udp *>(0x1); }
   void SetPeer(const char *ip, uint16 port) {
      _peer_addr.sin_family = AF_INET;
      _peer_addr.sin_port = htons(port);
      inet_pton(AF_INET, ip, &_peer_addr.sin_addr.s_addr);
   }

   void SetRemoteMagic(uint16 magic) { _remote_magic_number = magic; }
   void SetNextRecvSeq(uint16 seq) { _next_recv_seq = seq; }
   uint16 NextRecvSeq() const { return _next_recv_seq; }
   unsigned int LastRecvTime() const { return _last_recv_time; }

   void PushPendingFrame(int frame) {
      GameInput input;
      char bits = 0;
      input.init(frame, &bits, 1);
      _pending_output.push(input);
   }

   int PendingOutputSize() { return _pending_output.size(); }
   int PendingOutputFrontFrame() { return _pending_output.front().frame; }
   int LastAckedFrame() { return _last_acked_input.frame; }
};

bool Check(bool condition, const char *message) {
   if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      return false;
   }
   return true;
}

bool TestUdpMsgSizing() {
   UdpMsg keep_alive(UdpMsg::KeepAlive);
   if (!Check(keep_alive.PayloadSize() == 0, "KeepAlive payload should be empty")) return false;

   UdpMsg input(UdpMsg::Input);
   input.u.input.num_bits = 9;
   int expected_payload = (int)((char *)&input.u.input.bits - (char *)&input.u.input) + 2;
   if (!Check(input.PayloadSize() == expected_payload, "Input payload should include rounded bit payload")) return false;
   if (!Check(input.PacketSize() == (int)sizeof(input.hdr) + expected_payload, "Packet size should include header + payload")) return false;
   return true;
}

bool TestHandlesMsgFiltersByEndpoint() {
   TestUdpProtocol protocol;
   protocol.SetActive();
   protocol.SetPeer("127.0.0.1", 7000);

   sockaddr_in from = {};
   from.sin_family = AF_INET;
   from.sin_port = htons(7000);
   inet_pton(AF_INET, "127.0.0.1", &from.sin_addr.s_addr);

   UdpMsg msg(UdpMsg::KeepAlive);
   if (!Check(protocol.HandlesMsg(from, &msg), "Expected matching peer endpoint to be handled")) return false;

   from.sin_port = htons(7001);
   if (!Check(!protocol.HandlesMsg(from, &msg), "Expected non-matching peer endpoint to be ignored")) return false;
   return true;
}

bool TestOnMsgRejectsWrongMagic() {
   TestUdpProtocol protocol;
   protocol.SetActive();
   protocol.SetRemoteMagic(77);
   protocol.SetNextRecvSeq(10);

   UdpMsg msg(UdpMsg::Input);
   msg.hdr.magic = 99;
   msg.hdr.sequence_number = 10;

   unsigned int before_time = protocol.LastRecvTime();
   protocol.OnMsg(&msg, msg.PacketSize());

   if (!Check(protocol.NextRecvSeq() == 10, "Wrong-magic packet should not update recv sequence")) return false;
   if (!Check(protocol.LastRecvTime() == before_time, "Wrong-magic packet should not update receive timestamp")) return false;
   return true;
}

bool TestOnMsgDropsOutOfOrderPacket() {
   TestUdpProtocol protocol;
   protocol.SetActive();
   protocol.SetRemoteMagic(55);
   protocol.SetNextRecvSeq(10);

   UdpMsg msg(UdpMsg::Input);
   msg.hdr.magic = 55;
   msg.hdr.sequence_number = 9;

   protocol.OnMsg(&msg, msg.PacketSize());
   if (!Check(protocol.NextRecvSeq() == 10, "Out-of-order packet should be dropped")) return false;
   return true;
}

bool TestInputAckPrunesPendingFrames() {
   TestUdpProtocol protocol;
   protocol.PushPendingFrame(1);
   protocol.PushPendingFrame(2);
   protocol.PushPendingFrame(3);

   UdpMsg ack(UdpMsg::InputAck);
   ack.u.input_ack.ack_frame = 3;
   protocol.OnInputAck(&ack, ack.PacketSize());

   if (!Check(protocol.PendingOutputSize() == 1, "Ack should prune frames lower than ack_frame")) return false;
   if (!Check(protocol.PendingOutputFrontFrame() == 3, "Newest unacked frame should remain")) return false;
   if (!Check(protocol.LastAckedFrame() == 2, "Last acked frame should track highest pruned frame")) return false;
   return true;
}

}  // namespace

int main() {
   bool ok = true;
   ok = TestUdpMsgSizing() && ok;
   ok = TestHandlesMsgFiltersByEndpoint() && ok;
   ok = TestOnMsgRejectsWrongMagic() && ok;
   ok = TestOnMsgDropsOutOfOrderPacket() && ok;
   ok = TestInputAckPrunesPendingFrames() && ok;

   if (!ok) {
      return 1;
   }
   std::cout << "All protocol tests passed." << std::endl;
   return 0;
}
