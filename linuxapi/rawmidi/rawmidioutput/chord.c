// Plays a chord through the raw MIDI Output that is specified
// on the command line (ie, 0,0 to play from the first card's
// first MIDI output). If no output is specified, then it plays
// through the first MIDI output it finds.
//
// This uses the ALSA rawmidi API to demonstrate how to output
// MIDI events via that API (as opposed to the sequencer API).
// With the raw API, we have to do all the timing of events
// ourselves. In this cheesy example, I simply use sleep()
// to delay inbetween notes.
//
// Compile as:
// gcc -o chord chord.c -lasound

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <alsa/asoundlib.h>





/****************** find_midi_out() *********************
 * Finds the first MIDI output in the system, and copies
 * its name (for snd_rawmidi_open) to the specified
 * buffer. If no MIDI output is found, zeroes out the
 * buffer.
 */

void find_midi_out(char *cardName)
{
	register int			err;
	int						cardNum;
	snd_rawmidi_info_t	*rawMidiInfo;

	// Assume no output found
	cardName[0] = 0;

	// Start with first card
	cardNum = -1;

	// To get some info about the subdevices of a MIDI device, we need a
	// snd_rawmidi_info_t, so let's allocate one on the stack
	snd_rawmidi_info_alloca(&rawMidiInfo);
	memset(rawMidiInfo, 0, snd_rawmidi_info_sizeof());

	do
	{
		// Get next sound card's card number. When "cardNum" == -1, then ALSA
		// fetches the first card
		if ((err = snd_card_next(&cardNum)) < 0) break;

		// Another card? ALSA sets "cardNum" to -1 if no more
		if (cardNum != -1)
		{
			snd_ctl_t			*midiHandle;

			// Open this card. We specify only the card number -- not any device nor sub-device too
			sprintf(cardName, "hw:%i", cardNum);
			if ((err = snd_ctl_open(&midiHandle, cardName, 0)) >= 0)
			{
				int				devNum;

				// Start with the first device on this card, and look for a MIDI output
				devNum = -1;
				for (;;)
				{
					// Get the number of the next MIDI device on this card
					if ((err = snd_ctl_rawmidi_next_device(midiHandle, &devNum)) < 0 ||

					// No more MIDI devices on this card? ALSA sets "devNum" to -1 if so.
					// NOTE: It's possible that this sound card may have no MIDI devices on it
					// at all, for example if it's only a digital audio card
						devNum < 0)
					{	
						 break;
					}

					// Tell ALSA which device (number) we want info about
					snd_rawmidi_info_set_device(rawMidiInfo, devNum);

					// Get info on the MIDI out portion of this device
					snd_rawmidi_info_set_stream(rawMidiInfo, SND_RAWMIDI_STREAM_OUTPUT);

					// Tell ALSA to fill in our snd_rawmidi_info_t with info on the first
					// MIDI out subdevice
					snd_rawmidi_info_set_subdevice(rawMidiInfo, 0);
					if ((err = snd_ctl_rawmidi_info(midiHandle, rawMidiInfo)) >= 0 && snd_rawmidi_info_get_subdevices_count(rawMidiInfo))
					{
						// We found a MIDI Output device. Format its name in the caller's buffer
						sprintf(cardName, "hw:%i,%i", cardNum, devNum);

						// All done
						cardNum = -1;

						break;
					}
				}

				// Close the card after we're done enumerating its subdevices
				snd_ctl_close(midiHandle);
			}
		}

		// Another card?
	} while (cardNum != -1);
out:
	// ALSA allocates some mem to load its config file when we call some of the
	// above functions. Now that we're done getting the info, let's tell ALSA
	// to unload the info and free up that mem
	snd_config_update_free_global();
}





int main(int argc, char** argv)
{
	register int		err;
	snd_rawmidi_t		*midiOutHandle;
	char					cardName[64];

	// Did user supply a MIDI Output? If not, we need to find one	
	if (argc < 2)
	{
		find_midi_out(&cardName[0]);
		if (!cardName[0])
		{
			printf("Can't find a MIDI Output to play through!\n");
			return 1;
		}
	}
	
	// Use the one he supplied
	else
		sprintf(&cardName[0], "hw:%s", argv[1]);

	// Open output MIDI device
	if ((err = snd_rawmidi_open(NULL, &midiOutHandle, &cardName[0], 0)) < 0)
	{
		printf("Can't open MIDI Output %s: %s\n", &cardName[0], snd_strerror(err));
		return 1;
	}

	printf("Playing a chord on %s...\n", &cardName[0]);

	// We are sending all note-on events (note-offs will be a note-on with 0 velocity)
	cardName[0] = 0x90;

	// Play middle C (on the first MIDI channel) at a velocity of 100
	cardName[1] = 60;
	cardName[2] = 100;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);
	snd_rawmidi_drain(midiOutHandle);

	// Delay for a second and then turn the above note off
	sleep(1);

	// Play an E
	cardName[1] = 65;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);
	sleep(1);

	// Play a G
	cardName[1] = 69;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);
	sleep(3);

	// Turn off the above 3 notes
	cardName[2] = 0;
	cardName[1] = 60;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);
	cardName[1] = 65;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);
	cardName[1] = 69;
	snd_rawmidi_write(midiOutHandle, &cardName[0], 3);

	// Close the MIDI Output
	snd_rawmidi_close(midiOutHandle);

	return 0;
}
