# nrf24_bed_remote_hacking

OKIN/JLDK bed remote receiver sketch
for arduino & nRF24L01

includes models like RF358A, RF358C, RF502A, at al,
which are rebranded by a ton of bed manufacturers
like Serta, Sealy, Tempurpedic, Beautyrest, Ergomotion, et al
used for controlling adjustable bed bases

## description
waits for pairing request from remote
then listens on address and channel requested by remote
outputs messages received by remote

## motivation
My new bed with a single base came with only a single remote and receiver, and I have been looking for a way to pair multiple remotes.
(My previous bed base used dual twin bases, with two remotes - but those had two separate receivers linked with a cable.)

### initial findings
Bad news: Turns out due to the way the remote dictates the channel & address during pairing, it might not be straightforward to build a device to force them to the same channel/address, synchronizing multiple remotes to a single receiver.
It does appear to be straightforward to mimic the remote pairing sequence with a base receiver which has been put into pairing mode, and emulate a remote.
I'm still thinking of alternative scenarios that don't involve two radios (one for each remote). The nrf radios can listen on at least two completely distinct addresses, but only one channel at a time.
One complex idea with a single radio would be to allow double pairing, and have the unit quickly alternate between the two remote's channels, but not sure if that would work well.

## pairing process
pressing and holding button on the back of the remote will cause remote to begin rolling
sequentially through all channels, and transmitting pairing offer to pipe addr `0x9669966994`
(appears that some remotes roll through additional pipe sub-addresses prefixed w/ `0x96699669`,
e.g. 0x90, 0x91, 0x95 but 0x94 seemed to be common to the remotes I have)

once the pairing sequence packets are all ack'ed by a receiver, remote will switch to this
new address and channel and transmit there, until re-paired

pairing light flashes while searching, is steady while going through pairing sequence, and 
then will turn off when pairing is completed. 

remotes already paired with a base may just re-pair on same channel & address if base is still listening,
and may not try to pair on the pairing address.
also I have seen some remotes which periodically do this re-pairing sequence (with already paired base),
when just short-pressing the pair button. Perhaps this might be some quick re-pairing sequence, 
e.g. in case of interference on same channel, etc.

## listen mode
I added a special listen mode which will listen for, and pair with a remote
but then will itself try to emulate a remote and pair with a base
using same channel & address, then just eavesdrop on the conversation

## buttons
mystery: on some remotes these seem to have two-way feedback, e.g. fan & vibrator buttons cycle through modes/levels, and an indicator lights up and cycles on the remote as well (at least when using a real base receiver). I suspect there is either some ack payload, or perhaps some response on a nearby pipe (base address) a real base receiver transmits on to update remote about bed's state.

## disclaimer
Use at your own risk. I take no responsibility for this code, which is provided as-is, and for research purposes only.
Not affiliated with, or endorsed by, and aforementioned manufacturers or brands.