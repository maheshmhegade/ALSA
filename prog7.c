// listpcm.c
// Lists the hardware names of wave output device/sub-devices
// upon each ALSA sound card in the system.
//
// Compile as:
// gcc -o listpcm listpcm.c -lasound

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

      // Start with the first wave device on this card
      devNum = -1;
		
      for (;;)
      {
         snd_pcm_info_t  *pcmInfo;
         register int        subDevCount, i;

         // Get the number of the next wave device on this card
         if ((err = snd_ctl_pcm_next_device(cardHandle, &devNum)) < 0)
         {
            printf("Can't get next wave device number: %s\n", snd_strerror(err));
            break;
         }

         // No more wave devices on this card? ALSA sets "devNum" to -1 if so.
         // NOTE: It's possible that this sound card may have no wave devices on it
         // at all, for example if it's only a MIDI card
         if (devNum < 0) break;

         // To get some info about the subdevices of this wave device (on the card), we need a
         // snd_pcm_info_t, so let's allocate one on the stack
         snd_pcm_info_alloca(&pcmInfo);
         memset(pcmInfo, 0, snd_pcm_info_sizeof());

         // Tell ALSA which device (number) we want info about
         snd_pcm_info_set_device(pcmInfo, devNum);

         // Get info on the wave outs of this device
         snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_PLAYBACK);

         i = -1;
         subDevCount = 1;

         // More subdevices?
         while (++i < subDevCount)
         {
            // Tell ALSA to fill in our snd_pcm_info_t with info on this subdevice
            snd_pcm_info_set_subdevice(pcmInfo, i);
            if ((err = snd_ctl_pcm_info(cardHandle, pcmInfo)) < 0)
            {
               printf("Can't get info for wave output subdevice hw:%i,%i,%i: %s\n", cardNum, devNum, i, snd_strerror(err));
               continue;
            }

            // Print out how many subdevices (once only)
            if (!i)
            {
               subDevCount = snd_pcm_info_get_subdevices_count(pcmInfo);
               printf("\nFound %i wave output subdevices on card %i\n", subDevCount, cardNum);
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


snd_pcm_t    *pcmOutHandle;
register int err;

// Open output wave device hw:0,0
if ((err = snd_pcm_open(&pcmOutHandle, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
   printf("Can't open wave output: %s\n", snd_strerror(err));

snd_pcm_close(pcmOutHandle);


int set_audio_hardware(void)
{
   register int        err;
   snd_pcm_hw_params_t *hw_params;

   // We need to get an ALSA "sound hardware struct" in order to change any of
   // the hardware parameters of the sound card. We must ask ALSA to allocate
   // this struct for us. We can't declare one of them on our own. ALSA doesn't
   // allow us to know anything about what fields are inside one of these structs,
   // nor even what the sizeof the struct is. So the only way to get one of these
   // structs is to call this ALSA function to allocate one
   if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
   {
      printf("Can't get sound hardware struct %s\n", snd_strerror(err));
bad1: return(err);
   }

   // Fill in the sound hardware struct with the current hardware parameters for
   // the audio card. We'll going to alter a few fields, but leave the rest of
   // the fields at their current values. So, we need to fill in all of the fields
   // to their current values. Calling this ALSA function does that
   if ((err = snd_pcm_hw_params_any(pcmOutHandle, hw_params)) < 0)
   {
      printf("Can't init sound hardware struct: %s\n", snd_strerror(err));
bad2: snd_pcm_hw_params_free(hw_params);
      goto bad1;
   }

   // We want to set the hardware struct's "bit resolution" field to SND_PCM_FORMAT_S16_LE (16-bit)
   if ((err = snd_pcm_hw_params_set_format(pcmOutHandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
   {
      printf("Can't set 16-bit: %s\n", snd_strerror(err));
      goto bad2;
   }

   // We want to set the hardware struct's "rate"field to 44100
   if ((err = snd_pcm_hw_params_set_rate(pcmOutHandle, hw_params, 44100, 0)) < 0)
   {
      printf("Can't set sample rate: %s\n", snd_strerror(err));
      goto bad2;
   }

   // We want stereo playback, so we want to set the hardware struct's "channels"
   // field to 2
   if ((err = snd_pcm_hw_params_set_channels(pcmOutHandle, hw_params, 2)) < 0)
   {
      printf("Can't set stereo: %s\n", snd_strerror(err));
      goto bad2;
   }

   // Ok, we've changed the fields of the hardware struct. Now we need to tell ALSA
   // to give all of these updated hardware parameters back to the audio card, so that
   // the audio card can update its own parameters to what we requested. The following
   // ALSA function does this. NOTE: If the hardware doesn't support the parameters we
   // requested (for example, can't do 44100 rate), then we'll get an error. If we
   // instead use the ALSA "plughw" device (instead of "hw"), then ALSA will do
   // some realtime "translating" of our data to accomodate the sound card's limits,
   // such as maybe downsampling/upsampling to whatever nearest rate the sound card
   // supports. The downside of this is that it introduces extra overhead during
   // playback which can result in underrun errors, anti-aliasing errors being heard
   // in the data, etc
   if ((err = snd_pcm_hw_params(pcmOutHandle, hw_params)) < 0)
   {
      printf("Can't set hardware params: %s\n", snd_strerror(err));
      goto bad2;
   }

   // Now that we're set the hardware parameters, we don't need the hardware
   // struct any more. We tell ALSA to free it with the following call
   snd_pcm_hw_params_free(hw_params);

   // Success
   return(0);
}