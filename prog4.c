// listmidi.c
// Lists the hardware names of MIDI output device/sub-devices
// upon each ALSA sound card in the system.
//
// Compile as:
// gcc -o listmidi listmidi.c -lasound

#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>

int main(int argc, char **argv)
{
   register int  err;
   int           cardNum;

   // Start with first card
   cardNum = -1;

   for (;;)
   {
      snd_ctl_t *cardHandle;

      // Get next sound card's card number. When "cardNum" == -1, then ALSA
      // fetches the first card
      if ((err = snd_card_next(&cardNum)) < 0)
      {
         printf("Can't get the next card number: %s\n", snd_strerror(err));
         break;
      }

      // No more cards? ALSA sets "cardNum" to -1 if so
      if (cardNum < 0) break;

      // Open this card's control interface. We specify only the card number -- not
      // any device nor sub-device too
      {
      char   str[64];

      sprintf(str, "hw:%i", cardNum);
      if ((err = snd_ctl_open(&cardHandle, str, 0)) < 0)
      {
         printf("Can't open card %i: %s\n", cardNum, snd_strerror(err));
         continue;
      }
      }

      {
      int      devNum;

      // Start with the first MIDI device on this card
      devNum = -1;
		
      for (;;)
      {
         snd_rawmidi_info_t  *rawMidiInfo;
         register int        subDevCount, i;

         // Get the number of the next MIDI device on this card
         if ((err = snd_ctl_rawmidi_next_device(cardHandle, &devNum)) < 0)
         {
            printf("Can't get next MIDI device number: %s\n", snd_strerror(err));
            break;
         }

         // No more MIDI devices on this card? ALSA sets "devNum" to -1 if so.
         // NOTE: It's possible that this sound card may have no MIDI devices on it
         // at all, for example if it's only a digital audio card
         if (devNum < 0) break;

         // To get some info about the subdevices of this MIDI device (on the card), we need a
         // snd_rawmidi_info_t, so let's allocate one on the stack
         snd_rawmidi_info_alloca(&rawMidiInfo);
         memset(rawMidiInfo, 0, snd_rawmidi_info_sizeof());

         // Tell ALSA which device (number) we want info about
         snd_rawmidi_info_set_device(rawMidiInfo, devNum);

         // Get info on the MIDI outs of this device
         snd_rawmidi_info_set_stream(rawMidiInfo, SND_RAWMIDI_STREAM_OUTPUT);

         i = -1;
         subDevCount = 1;

         // More subdevices?
         while (++i < subDevCount)
         {
            // Tell ALSA to fill in our snd_rawmidi_info_t with info on this subdevice
            snd_rawmidi_info_set_subdevice(rawMidiInfo, i);
            if ((err = snd_ctl_rawmidi_info(cardHandle, rawMidiInfo)) < 0)
            {
               printf("Can't get info for MIDI output subdevice hw:%i,%i,%i: %s\n", cardNum, devNum, i, snd_strerror(err));
               continue;
            }

            // Print out how many subdevices (once only)
            if (!i)
            {
               subDevCount = snd_rawmidi_info_get_subdevices_count(rawMidiInfo);
               printf("\nFound %i MIDI output subdevices on card %i\n", subDevCount, cardNum);
            }

            // NOTE: If there's only one subdevice, then the subdevice number is immaterial,
            // and can be omitted when you specify the hardware name
            printf((subDevCount > 1 ? "    hw:%i,%i,%i\n" : "    hw:%i,%i\n"), cardNum, devNum, i);
         }
      }
      }

      // Close the card's control interface after we're done with it
      snd_ctl_close(cardHandle);
   }

   snd_config_update_free_global();
}

snd_rawmidi_t *midiOutHandle;

// Open output MIDI device hw:0,0,0
if ((err = snd_rawmidi_open(0, &midiOutHandle, "hw:0,0,0", 0)) < 0)
{
   printf("Can't open MIDI output: %s\n", snd_strerror(err));
}

unsigned char buffer[3];

// Send a middle C note on the first MIDI channel, velocity of 100
buffer[0] = 0x90;
buffer[1] = 60;
buffer[2] = 100;
snd_rawmidi_write(midiOutHandle, buffer, 3);

// Send an E note on the same MIDI channel. Note: Only 2 bytes output
buffer[0] = 64;
buffer[1] = 100;
snd_rawmidi_write(midiOutHandle, buffer, 2);
Note that a single call to snd_rawmidi_write could reference a buffer containing numerous MIDI messages, with running status too. For example, here we send both note-on messages in one buffer, with a single call to snd_rawmidi_write:

unsigned char buffer[5];

// Buffer contains 2 MIDI messages, total of 5 bytes using running status
buffer[0] = 0x90;
buffer[1] = 60;
buffer[2] = 100;
buffer[3] = 64;
buffer[4] = 100;
snd_rawmidi_write(midiOutHandle, buffer, 5);

snd_rawmidi_params_t *params;
register int err;

// Allocate a snd_rawmidi_params_t from ALSA
if ((err = snd_rawmidi_params_current(¶ms)))
   printf("Can't get a snd_rawmidi_params_t: %s\n", snd_strerror(err));
else
{
   // Tell ALSA to fill in our snd_rawmidi_params_t with this MIDI out's parameters
   snd_rawmidi_params_current(midiOutHandle, params);

   // Display the driver's buffer size for this MIDI output
   printf("Buffer size = %i\n", snd_rawmidi_params_get_buffer_size(params));

   // Set the driver's buffer size to 10000 bytes. More fucking abstraction!
   snd_rawmidi_params_set_buffer_size(midiOutHandle, params, 10000);
   snd_rawmidi_params(midiOutHandle, params);

   // Free our snd_rawmidi_params_t
   snd_rawmidi_params_free(params);
}
snd_rawmidi_close(midiOutHandle);


// Enable non-blocking mode
snd_rawmidi_nonblock(midiOutHandle, 1);


// Open output MIDI device hw:0,0,0 in non-blocking mode
if ((err = snd_rawmidi_open(0, &midiOutHandle, "hw:0,0,0", SND_RAWMIDI_NONBLOCK)) < 0)
{
   // Is someone else using this MIDI output?
   if (err == -EBUSY)
   {
      // Here you may wish to try opening a different MIDI output
   }
   printf("Can't open MIDI output: %s\n", snd_strerror(err));
}
// Here you may wish to turn off non-blocking mode
else
   snd_rawmidi_nonblock(midiOutHandle, 0);



snd_rawmidi_drain(midiOutHandle);

