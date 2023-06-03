/*
  okin/jldk bed remote receiver sketch
  for arduino & nRF24L01

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
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

//
// Hardware configuration
//

// bed remote stuff
/// message format
// 0: message body size
// 1: message counter
// 2: message type?
// 3: message body 

// known message types
#define   MT_NORMAL     0x03
#define   MT_PAIR_INIT  0x04
#define   MT_PAIR_DONE  0x05

// Set up nRF24L01 radio on SPI bus plus pins 7 & 8
RF24 radio(7,8);

uint8_t buf[32];
uint8_t msgbuf[32];
uint8_t pipenum;

uint8_t newpipe[5] = {0,0,0,0,0};

bool pairing_started = false;
uint8_t pairing_ctr;

uint8_t chan = 1; // initial listen channel

const uint8_t addresses[][5] = {
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
  radio.setAutoAck(true); // default
  radio.setCRCLength(RF24_CRC_16); // default
  radio.setDataRate(RF24_1MBPS);
  radio.enableDynamicPayloads(); // this might need to be flipped for clone radios?

  radio.stopListening();
  radio.openReadingPipe(0, addresses[0]);
  radio.openReadingPipe(1, addresses[1]);
  radio.setChannel(chan);
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();
  radio.printPrettyDetails();
}

void loop(void)
{
  if (radio.available(&pipenum))
    {
      // read msg header
      radio.read(&buf, radio.getDynamicPayloadSize());
      uint8_t msgsize = buf[0];
      uint8_t msgcounter = buf[1];
      uint8_t msgtype = buf[2];
      
      // read msg body
      memcpy(msgbuf, &buf[3], msgsize);

      uint32_t ts = millis();
      printf("t=%08lu rxp=%i, ch=%02d, msz=%02d, ctr=%03d, mt=%02d: ",
        ts, pipenum, chan, msgsize, msgcounter, msgtype);
      for (uint8_t i=0; i < msgsize; i++) {
        printf("%02X ", msgbuf[i]);
      }
      printf("\n");

      if (msgtype == MT_NORMAL) {
        //uint8_t rbuf[4] = {0x05, 1, 0x01, 0x02};
        //radio.writeAckPayload(4, &rbuf, 4);
      }
      else if (msgtype == MT_PAIR_INIT) 
      {
        // received a message type for pair init
        // remote dictates channel and pipe to move to
        // as a receiver, we just follow orders
        // get channel & rx pipe addr from message
        // also track msgctr - should not change over pairing sequence

        printf("received pair init request\n");
        pairing_started = true;       
        pairing_ctr = msgcounter;
        chan = msgbuf[0];
        // I suspect the remote pipe address is the last 5 bytes, i.e. msgbuf[2] through [5]
        // and msgbuf[1] is the "offset" above base address which bed receiver should listen
        // but this "offset" always seems to be 4, in my limited test samples
        uint8_t offset = msgbuf[1];
        newpipe[0] = msgbuf[2] + offset;
        newpipe[1] = msgbuf[3];
        newpipe[2] = msgbuf[4];
        newpipe[3] = msgbuf[5];
        newpipe[4] = msgbuf[6];
      } 
      else if (msgtype == MT_PAIR_DONE && pairing_started && pairing_ctr == msgcounter) 
      {
        // change pipe addr & chan
        radio.openReadingPipe(1, newpipe);
        radio.setChannel(chan);
        printf("switched to chan: %02d, rxpipe: ", chan);
        for (uint8_t i=0; i < sizeof(newpipe); i++) {
          printf("%02X ", newpipe[i]);
        }
        printf("\n");
        pairing_started = false;
        //radio.printPrettyDetails();
      }
      else if (pairing_started && pairing_ctr != msgcounter)
      {
        // we missed pairing message sequence, reset state
        pairing_started = false;
        printf("pairing state reset\n");
      }

    }
}
