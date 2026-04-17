/* -----------------------------------------------------------------------
 * Integration tests: two GGPO P2P sessions communicating over loopback UDP
 * sockets in the same process.
 *
 * Each test creates two sessions, wires them to each other via 127.0.0.1,
 * and drives the standard GGPO frame loop until the desired condition is
 * observed (or a wall-clock deadline expires).
 *
 * Test layout
 * -----------
 * 1. TestTwoClientSynchronize  -- both sessions reach GGPO_EVENTCODE_RUNNING
 * 2. TestTwoClientInputExchange -- after sync, both advance at least 5 frames
 * 3. TestDisconnectNotification -- client 1 detects peer disconnect when
 *                                  client 0 is closed abruptly
 *
 * Ports used
 * ----------
 *   Test 1:  19601, 19602
 *   Test 2:  19603, 19604
 *   Test 3:  19605, 19606
 */

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

#include "types.h"
#include "platform_linux.h"
#include "ggponet.h"

/* -------------------------------------------------------------------------
 * Per-client test context
 * ---------------------------------------------------------------------- */

struct TestClient {
   GGPOSession      *session;
   GGPOPlayerHandle  local_handle;
   int               game_frame;       /* frames confirmed in the main loop */
   bool              running;          /* received GGPO_EVENTCODE_RUNNING */
   bool              got_disconnected; /* received GGPO_EVENTCODE_DISCONNECTED_FROM_PEER */

   void Reset() {
      session          = NULL;
      local_handle     = GGPO_INVALID_HANDLE;
      game_frame       = 0;
      running          = false;
      got_disconnected = false;
   }
};

/* Two global client slots.  Plain C function pointers dispatch to the right
 * context by index. */
static TestClient g_clients[2];

/* -------------------------------------------------------------------------
 * GGPOSessionCallbacks implementations
 * ---------------------------------------------------------------------- */

static bool begin_game(const char *) { return true; }

static bool save_game_state(unsigned char **buf, int *len, int *checksum, int frame)
{
   *buf = (unsigned char *)malloc(sizeof(int));
   if (!*buf) return false;
   memcpy(*buf, &frame, sizeof(int));
   *len      = sizeof(int);
   *checksum = frame;
   return true;
}

static bool load_game_state(unsigned char *, int) { return true; }
static bool log_game_state(char *, unsigned char *, int) { return true; }
static void free_buffer(void *buf) { free(buf); }

/* advance_frame callback -- called during rollbacks to re-simulate a frame.
 * Must call ggpo_synchronize_input (to get corrected inputs) and then
 * ggpo_advance_frame to step the GGPO frame counter forward. */
static bool advance_frame_impl(int idx, int)
{
   TestClient &c = g_clients[idx];
   uint8 inputs[2]    = { 0, 0 };
   int   disc_flags   = 0;
   ggpo_synchronize_input(c.session, inputs, (int)sizeof(inputs), &disc_flags);
   ggpo_advance_frame(c.session);
   return true;
}

static bool advance_frame_0(int flags) { return advance_frame_impl(0, flags); }
static bool advance_frame_1(int flags) { return advance_frame_impl(1, flags); }

static bool on_event_impl(int idx, GGPOEvent *evt)
{
   TestClient &c = g_clients[idx];
   switch (evt->code) {
   case GGPO_EVENTCODE_RUNNING:
      c.running = true;
      break;
   case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
      c.got_disconnected = true;
      break;
   default:
      break;
   }
   return true;
}

static bool on_event_0(GGPOEvent *evt) { return on_event_impl(0, evt); }
static bool on_event_1(GGPOEvent *evt) { return on_event_impl(1, evt); }

/* -------------------------------------------------------------------------
 * Test infrastructure helpers
 * ---------------------------------------------------------------------- */

static GGPOSessionCallbacks MakeCallbacks(int idx)
{
   GGPOSessionCallbacks cb = {};
   cb.begin_game      = begin_game;
   cb.save_game_state = save_game_state;
   cb.load_game_state = load_game_state;
   cb.log_game_state  = log_game_state;
   cb.free_buffer     = free_buffer;
   cb.advance_frame   = (idx == 0) ? advance_frame_0 : advance_frame_1;
   cb.on_event        = (idx == 0) ? on_event_0      : on_event_1;
   return cb;
}

/* Create two P2P sessions over loopback:
 *   client 0 -- local player 1 on port_a, remote player 2 at port_b
 *   client 1 -- remote player 1 at port_a, local player 2 on port_b */
static bool SetupTwoClients(unsigned short port_a, unsigned short port_b)
{
   g_clients[0].Reset();
   g_clients[1].Reset();

   GGPOSessionCallbacks cb0 = MakeCallbacks(0);
   GGPOSessionCallbacks cb1 = MakeCallbacks(1);

   /* --- Client 0 --- */
   if (ggpo_start_session(&g_clients[0].session, &cb0, "itest",
                          2, (int)sizeof(uint8), port_a) != GGPO_OK)
      return false;
   {
      GGPOPlayer p = {};
      p.size       = sizeof(GGPOPlayer);
      p.type       = GGPO_PLAYERTYPE_LOCAL;
      p.player_num = 1;
      if (ggpo_add_player(g_clients[0].session, &p,
                          &g_clients[0].local_handle) != GGPO_OK) return false;
   }
   {
      GGPOPlayer p = {};
      p.size       = sizeof(GGPOPlayer);
      p.type       = GGPO_PLAYERTYPE_REMOTE;
      p.player_num = 2;
      strncpy(p.u.remote.ip_address, "127.0.0.1",
              sizeof(p.u.remote.ip_address));
      p.u.remote.port = port_b;
      GGPOPlayerHandle h;
      if (ggpo_add_player(g_clients[0].session, &p, &h) != GGPO_OK) return false;
   }

   /* --- Client 1 --- */
   if (ggpo_start_session(&g_clients[1].session, &cb1, "itest",
                          2, (int)sizeof(uint8), port_b) != GGPO_OK)
      return false;
   {
      GGPOPlayer p = {};
      p.size       = sizeof(GGPOPlayer);
      p.type       = GGPO_PLAYERTYPE_REMOTE;
      p.player_num = 1;
      strncpy(p.u.remote.ip_address, "127.0.0.1",
              sizeof(p.u.remote.ip_address));
      p.u.remote.port = port_a;
      GGPOPlayerHandle h;
      if (ggpo_add_player(g_clients[1].session, &p, &h) != GGPO_OK) return false;
   }
   {
      GGPOPlayer p = {};
      p.size       = sizeof(GGPOPlayer);
      p.type       = GGPO_PLAYERTYPE_LOCAL;
      p.player_num = 2;
      if (ggpo_add_player(g_clients[1].session, &p,
                          &g_clients[1].local_handle) != GGPO_OK) return false;
   }

   return true;
}

static void TeardownClients()
{
   for (int i = 0; i < 2; i++) {
      if (g_clients[i].session) {
         ggpo_close_session(g_clients[i].session);
         g_clients[i].session = NULL;
      }
   }
}

typedef bool (*Predicate)();

/* Pump all live sessions until pred() is true or timeout_ms elapses. */
static bool PumpUntil(Predicate pred, int timeout_ms)
{
   uint32 deadline = Platform::GetCurrentTimeMS() + (uint32)timeout_ms;
   while (Platform::GetCurrentTimeMS() < deadline) {
      for (int i = 0; i < 2; i++) {
         if (g_clients[i].session)
            ggpo_idle(g_clients[i].session, 0);
      }
      if (pred()) return true;
   }
   return false;
}

static bool BothRunning()
{
   return g_clients[0].running && g_clients[1].running;
}

/* Perform one main-loop step for both running clients:
 *  1. submit local inputs
 *  2. pump the network layer
 *  3. synchronize and advance to the next confirmed frame */
static void StepBothClients()
{
   for (int i = 0; i < 2; i++) {
      if (g_clients[i].running) {
         uint8 input = (uint8)(i + 1); /* distinct per client */
         ggpo_add_local_input(g_clients[i].session,
                              g_clients[i].local_handle,
                              &input, (int)sizeof(uint8));
      }
   }
   for (int i = 0; i < 2; i++) {
      if (g_clients[i].session)
         ggpo_idle(g_clients[i].session, 0);
   }
   for (int i = 0; i < 2; i++) {
      if (g_clients[i].running) {
         uint8 inputs[2]  = { 0, 0 };
         int   disc_flags = 0;
         if (ggpo_synchronize_input(g_clients[i].session, inputs,
                                    (int)sizeof(inputs),
                                    &disc_flags) == GGPO_OK) {
            g_clients[i].game_frame++;
            ggpo_advance_frame(g_clients[i].session);
         }
      }
   }
}

static bool Check(bool condition, const char *message)
{
   if (!condition) {
      std::cerr << "FAIL: " << message << std::endl;
      return false;
   }
   return true;
}

/* -------------------------------------------------------------------------
 * Test 1: Two clients reach Running state via loopback UDP handshake
 * ---------------------------------------------------------------------- */

static bool TestTwoClientSynchronize()
{
   if (!Check(SetupTwoClients(19601, 19602),
              "SetupTwoClients should succeed")) return false;

   bool synced = PumpUntil(BothRunning, 5000);

   bool result =
      Check(synced,
            "Both clients should reach Running state within 5 seconds")
      && Check(g_clients[0].running, "Client 0 should report running")
      && Check(g_clients[1].running, "Client 1 should report running");

   TeardownClients();
   return result;
}

/* -------------------------------------------------------------------------
 * Test 2: Both clients exchange inputs and advance frames
 * ---------------------------------------------------------------------- */

static bool BothAdvancedFiveFrames()
{
   return g_clients[0].game_frame >= 5 && g_clients[1].game_frame >= 5;
}

static bool TestTwoClientInputExchange()
{
   if (!Check(SetupTwoClients(19603, 19604),
              "SetupTwoClients should succeed")) return false;

   if (!PumpUntil(BothRunning, 5000)) {
      TeardownClients();
      return Check(false, "Clients did not synchronize within 5 seconds");
   }

   uint32 deadline = Platform::GetCurrentTimeMS() + 5000;
   while (Platform::GetCurrentTimeMS() < deadline && !BothAdvancedFiveFrames())
      StepBothClients();

   bool result =
      Check(g_clients[0].game_frame >= 5,
            "Client 0 should advance at least 5 frames")
      && Check(g_clients[1].game_frame >= 5,
               "Client 1 should advance at least 5 frames");

   TeardownClients();
   return result;
}

/* -------------------------------------------------------------------------
 * Test 3: Client 1 detects a disconnect when client 0 stops responding
 * ---------------------------------------------------------------------- */

static bool Client1Disconnected() { return g_clients[1].got_disconnected; }

static bool TestDisconnectNotification()
{
   if (!Check(SetupTwoClients(19605, 19606),
              "SetupTwoClients should succeed")) return false;

   /* Short timeouts so the test finishes in reasonable time. */
   ggpo_set_disconnect_timeout(g_clients[0].session, 1000);
   ggpo_set_disconnect_timeout(g_clients[1].session, 1000);
   ggpo_set_disconnect_notify_start(g_clients[0].session, 200);
   ggpo_set_disconnect_notify_start(g_clients[1].session, 200);

   if (!PumpUntil(BothRunning, 5000)) {
      TeardownClients();
      return Check(false, "Clients did not synchronize within 5 seconds");
   }

   /* Advance a few frames so _last_recv_time is freshly stamped before the
    * connection is cut, ensuring the timeout starts from a known-recent
    * baseline. */
   uint32 warmup = Platform::GetCurrentTimeMS() + 1000;
   while (Platform::GetCurrentTimeMS() < warmup && g_clients[0].game_frame < 3)
      StepBothClients();

   /* Close client 0 abruptly -- no goodbye packet is sent.  Client 1 must
    * discover the silence by itself after the configured timeout. */
   ggpo_close_session(g_clients[0].session);
   g_clients[0].session = NULL;

   /* Pump client 1 until it raises GGPO_EVENTCODE_DISCONNECTED_FROM_PEER. */
   uint32 deadline = Platform::GetCurrentTimeMS() + 3000;
   while (Platform::GetCurrentTimeMS() < deadline) {
      ggpo_idle(g_clients[1].session, 0);
      if (g_clients[1].got_disconnected) break;
   }

   bool result = Check(g_clients[1].got_disconnected,
                       "Client 1 should detect client 0 disconnected within 3 seconds");

   TeardownClients();
   return result;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main()
{
   /* Platform::GetCurrentTimeMS() returns 0 on its very first call and
    * simultaneously initializes the monotonic clock baseline.  UdpProtocol
    * stores the send timestamp in _last_send_time and the retry guard reads
    *
    *   if (_last_send_time && _last_send_time + interval < now)
    *
    * Because 0 is treated as "never sent", a _last_send_time of 0 prevents
    * retries forever.  Priming the clock here ensures every subsequent call
    * returns a positive value, so _last_send_time will be non-zero when the
    * first SyncRequest is sent. */
   (void)Platform::GetCurrentTimeMS();
   while (Platform::GetCurrentTimeMS() == 0) { usleep(500); }

   bool ok = true;
   ok = TestTwoClientSynchronize()   && ok;
   ok = TestTwoClientInputExchange() && ok;
   ok = TestDisconnectNotification() && ok;

   if (!ok) return 1;
   std::cout << "All integration tests passed." << std::endl;
   return 0;
}
