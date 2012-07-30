snd_rawmidi_t *midiInHandle;
register int err;

// Open input MIDI device hw:0,0,0
if ((err = snd_rawmidi_open(&midiInHandle, 0, "hw:0,0,0", 0)) < 0)
{
   printf("Can't open MIDI input: %s\n", snd_strerror(err));
}


snd_rawmidi_t *midiInHandle;
snd_rawmidi_t *midiOutHandle;

snd_rawmidi_open(&midiInHandle, &midiOutHandle, "hw:0,0,0", 0);

unsigned char buffer;
register int err;

if ((err = snd_rawmidi_read(midiInHandle, &buffer, 1)) < 0)
{
   printf("Can't read MIDI input: %s\n", snd_strerror(err));
}


snd_rawmidi_status_t *ptr;
register int err;

// We need to get a snd_rawmidi_status_t struct
if ((err = snd_rawmidi_status_malloc(&ptr)) < 0)
   printf("Can't get snd_rawmidi_status_t: %s\n", snd_strerror(err));
else
{
   // Tell ALSA to fill in our snd_rawmidi_status_t struct with this device's status
   if ((err = snd_rawmidi_status(midiInHandle, ptr)) < 0)
      printf("Can't get status: %s\n", snd_strerror(err));
   else
   {
      err = snd_rawmidi_status_get_xruns(ptr);
      printf("There are %i errors\n", err);
   }

   // Free the snd_rawmidi_status_t struct when done
   snd_rawmidi_status_free(ptr);
}

snd_rawmidi_close(midiInHandle);

unsigned char buffer[10];
register int err;

err = snd_rawmidi_read(midiInHandle, &buffer[0], 10);
// In blocking mode, we don't get here until buffer[] contains 10 MIDI bytes

// Enable non-blocking mode
snd_rawmidi_nonblock(midiInHandle, 1);

// Open input MIDI device hw:0,0,0 in non-blocking mode
if ((err = snd_rawmidi_open(&midiInHandle, 0, "hw:0,0,0", SND_RAWMIDI_NONBLOCK)) < 0)
{
   // Is someone else using this MIDI input?
   if (err == -EBUSY)
   {
      // Here you may wish to try opening a different MIDI input
   }
   printf("Can't open MIDI input: %s\n", snd_strerror(err));
}
// Here you may wish to turn off non-blocking mode
else
   snd_rawmidi_nonblock(midiInHandle, 0);

