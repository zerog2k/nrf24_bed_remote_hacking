/*
  okin/jldk bed remote receiver sketch
  for arduino & nRF24L01
  copyright Jens Jensen 2023
  MIT LICENSE

  waits for pairing request from remote
  then listens on address and channel requested by remote

  pairing process:
  pressing and holding button on the back of the remote will cause remote to begin rolling
  sequentially through all channels, and transmitting pairing offer to pipe addr 0x9669966994
  once the pairing sequence packets are all ack'ed by a receiver, remote will switch to this
  new address and channel and transmit there, until re-paired
  pairing light flashes while searching, is steady while going through pairing sequence, and 
  then will turn off when pairing is completed. 
*/

#include <SPI.h>
//#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

// bed remote stuff
/// message header bytes
// 0: message body size
// 1: message counter
// 2: message type?
// 3+: message body

// known message types
#define   MT_NORMAL       0x03
#define   MT_PAIR_INIT    0x04
#define   MT_PAIR_DONE    0x05
#define   MT_BASE_STATUS  0x06

// known remote button masks, msgtype: 3
// msgbuf[0]
#define   K_FAN_RIGHT  0x40
#define   K_VIBRATE    0x04
#define   K_FLAT       0x08

// msgbuf[1]
#define   K_FAN_LEFT   0x40
#define   K_STAR       0x01
#define   K_LAMP       0x02
#define   K_SYNC       0x04

// msgbuf[2]
#define   K_ZEROG      0x10
#define   K_TV         0x40
#define   K_SNORE      0x80
#define   K_HEAD_VIBRATE 0x08
#define   K_FOOT_VIBRATE 0x04
// some remotes send this REMOTE_WOKE_UP bit, but some bases interpret it as a vibrate command
#define   REMOTE_WOKE_UP 0x01   // is remote asking for status update?
#define   FAN_STATUS    0x02    // is remote asking for fan status?

// msgbuf[3]
#define   K_HEAD_UP    0x01
#define   K_HEAD_DOWN  0x02
#define   K_FEET_UP    0x04
#define   K_FEET_DOWN  0x08

/*
 in listen only mode, we need to emulate a base
 so a remote can pair to us, and tell us which channel & pipe addr to listen on
 then we can emulate a remote, & try to pair with a base receiver,
 to move him to listen to the real remote
 then we sit back and listen in

 with listen_mode = false, we just emulate a base to pair with a remote
*/
const bool listen_mode = false;

#define MAX_CHANNELS  82

// Set up nRF24L01 radio on SPI bus plus pins 7 & 8
RF24 radio(7,8);

uint8_t buf[32];
uint8_t msgbuf[32];
uint8_t pipenum;

const uint8_t pairaddr[5] = {0x94, 0x69, 0x96, 0x69, 0x96};
uint8_t pipeaddr[5] = {0x12, 0x34, 0x56, 0x78, 0x90};
uint8_t pipeaddr_offset;

bool remote_pair_started = false;
bool remote_pair_done = false;
bool base_pair_done = false;

uint8_t pairing_ctr;
uint32_t ts = 0;
uint8_t chan = 1; // initial channel

const uint8_t addresses[][5] = {
  {0x94, 0x69, 0x96, 0x69, 0x96},
  {0x00, 0x69, 0x96, 0x69, 0x96},
  {0x96, 0x69, 0x96, 0x69, 0x96},
  {0x93, 0x69, 0x96, 0x69, 0x96},
  {0x94, 0x69, 0x96, 0x69, 0x96},
  {0x95, 0x69, 0x96, 0x69, 0x96},     
  }; 

//
// Setup
//

void setup(void)
{
  Serial.begin(115200);
  printf_begin();

  // Setup and configure nrf24 radio

  radio.begin();
  radio.setDataRate(RF24_1MBPS);
  radio.setAutoAck(true);
  radio.setCRCLength(RF24_CRC_16);
  radio.enableDynamicPayloads(); // this might need to be flipped for clone radios?
  radio.stopListening();
  radio.openReadingPipe(0, pairaddr);
  //radio.openReadingPipe(1, addresses[1]);
  //radio.openReadingPipe(2, addresses[2]);
  //radio.openReadingPipe(3, addresses[3]);
  //radio.openReadingPipe(4, addresses[4]);
  //radio.openReadingPipe(5, addresses[5]);  

  radio.setChannel(chan);
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();
  radio.printPrettyDetails();
  printf("listen_mode: %d\n", listen_mode);
}

void loop(void)
{
  // regular listen mode
  if (radio.available(&pipenum))
  {
    // read msg header
    uint8_t dplsz = radio.getDynamicPayloadSize();
    radio.read(&buf, dplsz);
    uint8_t msgsize = buf[0];
    uint8_t msgcounter = buf[1];
    uint8_t msgtype = buf[2];

    // read msg body
    memcpy(msgbuf, &buf[3], msgsize);

    ts = millis();
    printf("t=%08lu, rxp=%i, ch=%02d, msgsz=%02d, ctr=%03d, mt=%02d: ",
      ts, pipenum, chan, msgsize, msgcounter, msgtype);
    for (uint8_t i=0; i < msgsize; i++) {
      printf("%02X ", msgbuf[i]);
    }
    printf("\n");

    if (msgtype == MT_NORMAL) {
      // send ack payload in response, e.g. to change remote lights
      if (msgbuf[0] & 0x04 && ! listen_mode)
      {
        radio.enableAckPayload();
        // msg body status bytes: [ fan_left, fan_right, ?, ?, vibrate, ?, ?, ? ]
        uint8_t rbuf[] = {0x08, 0, MT_BASE_STATUS, 2, 3, 0, 0, 1, 0, 0, 0};
        radio.writeAckPayload(1, &rbuf, sizeof(rbuf));
      }
    }
    else if (msgtype == MT_PAIR_INIT) 
    {
      // received a message type for pair init
      // a remote decides the channel and pipe to move to
      // as a receiver, we just follow orders
      // get channel & rx pipe addr from message
      // also track msgctr - should not change over pairing sequence
      remote_pair_started = true;       
      pairing_ctr = msgcounter;
      chan = msgbuf[0];
      // I suspect the remote pipe address is the last 5 bytes, i.e. msgbuf[2] through [5]
      // and msgbuf[1] is the "offset" above base address which bed receiver should listen
      // but this "offset" always seems to be 4, in my limited testing
      pipeaddr_offset = msgbuf[1];
      pipeaddr[0] = msgbuf[2] + pipeaddr_offset;
      pipeaddr[1] = msgbuf[3];
      pipeaddr[2] = msgbuf[4];
      pipeaddr[3] = msgbuf[5];
      pipeaddr[4] = msgbuf[6];
      printf("remote pair init request\n");
    } 
    else if (msgtype == MT_PAIR_DONE && remote_pair_started && pairing_ctr == msgcounter) 
    {
      // change pipe addr & chan
      radio.openReadingPipe(1, pipeaddr);  
      radio.setChannel(chan);        
      printf("remote pair done, switched to chan: %02d, rxpipe: ", chan);
      for (uint8_t i=0; i < sizeof(pipeaddr); i++) {
        printf("%02X ", pipeaddr[i]);
      }
      printf("\n");
      remote_pair_started = false;
      remote_pair_done = true;
      if (pipenum == 0)
      {
        // this came from pairing address
        // we probably re-paired with a new remote
        // so initiate re-pair with base
        base_pair_done = false;      
      }
      //radio.printPrettyDetails();
    }
    else if (remote_pair_started && pairing_ctr != msgcounter)
    {
      // we missed pairing message sequence, reset state
      remote_pair_started = false;
      printf("remote pairing state reset\n");
    }
  }


  // for listen only mode, now that we are paired with remote
  // try to emulate a remote and force a base receiver to listen
  // on same pipeaddr & chan
  if (listen_mode && remote_pair_done && ! base_pair_done) 
  {
    // first, emulate a remote, and pair with base receiver
    // this will force base to dwell on a channel we know about
    // so we can then listen to real remote pairing
    Serial.println("now starting pair attempt with base receiver... ");
    radio.setAutoAck(true);
    while (! base_pair_done)
    {
      for (uint8_t i = 0; i < MAX_CHANNELS; i++)
      {
        if (base_pair_done)
          break;
        // roll through channels and broadcast pairing request
        // until we find a base accepting our requests
        radio.setChannel(i);
        radio.stopListening();
        radio.openWritingPipe(pairaddr); // 0x9669966994
        // msg header    
        buf[0] = 0x04; // msg body size
        buf[1] = 0x00; // msg counter, dont care
        buf[2] = MT_NORMAL; // msg type, normal ?
        // msg body
        buf[3] = 0;    // dont care
        buf[4] = 0;
        buf[5] = 0;
        buf[6] = 0; 
        // Send a dummy message. If ack'ed we know
        // we are on the channel where base is listening
        // some bases want to see multiple requests
        if (radio.write(buf, 7) && radio.write(buf, 7))
        {
          // msg header
          buf[0] = 0x07; // msg body size
          buf[1] = 0x00; // msg counter, dont care
          buf[2] = MT_PAIR_INIT; // msg type: pair init
          // msg body
          buf[3] = chan; // desired channel
          buf[4] = pipeaddr_offset; // desired base pipe addr offset
          buf[5] = pipeaddr[0] - pipeaddr_offset;
          buf[6] = pipeaddr[1];
          buf[7] = pipeaddr[2];
          buf[8] = pipeaddr[3];
          buf[9] = pipeaddr[4];
          printf("send pair init. ");
          // bases likes it sent multiple times ?
          if (radio.write(buf, 10) && radio.write(buf, 10) && radio.write(buf, 10))
          {
            // msg header
            buf[0] = 0x01; // msg body size
            buf[1] = 0x00; // msg counter, dont care
            buf[2] = MT_PAIR_DONE; // msg type
            // msg body
            buf[3] = 1; // success?         
            printf("send pair done. ");
            // some bases like it multiple times ?
            if (radio.write(buf, 4) && radio.write(buf, 4))
            {
              printf("ack'd successfully.\n");
              base_pair_done = true;
            }
            else
            {
              printf("NO ack from receiver.\n");
            }
          }
        }
        radio.startListening();
      }
      if (! base_pair_done)
        printf("no receiver found in this pass.\n");
      delay(1000);
    }

    // ensure we are back on remotes chan & addr
    radio.setAutoAck(1, false); // just listen, don't ack
    radio.setChannel(chan); // move back to remote chan
    radio.openReadingPipe(1, pipeaddr);
    printf("base pair done?: %d\n", base_pair_done);
  }
}
