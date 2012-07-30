// Demonstrates receiving MIDI input through the raw MIDI
// Input that is specified on the command line (ie, 0,0 to receive
// from the first card's first MIDI input). If no input is
// specified, then it receives through the first MIDI input it
// finds.
//
// This uses the ALSA rawmidi API to demonstrate how to input
// MIDI events via that API (as opposed to the sequencer API).
// With the raw API, we read the unadorned MIDI bytes -- no
// timestamps, (perhaps) no resolved running status, etc.
//
// Compile as:
// gcc -o rawmidiinput rawmidiinput.c -lasound


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <alsa/asoundlib.h>



// Set to 1 if user wants to abort
int StopFlag = 0;






/****************** sighandler() *********************
 * Called by the operating system when the user presses
 * CTRL-C to abort this app.
 */

void sighandler(int dummy)
{
	// Let main() know that user wants to break out of the loop
	StopFlag = 1;
}





/****************** find_midi_in() *********************
 * Finds the first MIDI input in the system, and copies
 * its name (for snd_rawmidi_open) to the specified
 * buffer. If no MIDI input is found, zeroes out the
 * buffer.
 */

void find_midi_in(char *cardName)
{
	register int			err;
	int						cardNum;
	snd_rawmidi_info_t	*rawMidiInfo;

	// Assume no input found
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

				// Start with the first device on this card, and look for a MIDI input
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

					// Get info on the MIDI in portion of this device
					snd_rawmidi_info_set_stream(rawMidiInfo, SND_RAWMIDI_STREAM_INPUT);

					// Tell ALSA to fill in our snd_rawmidi_info_t with info on the first
					// MIDI in subdevice
					snd_rawmidi_info_set_subdevice(rawMidiInfo, 0);
					if ((err = snd_ctl_rawmidi_info(midiHandle, rawMidiInfo)) >= 0 && snd_rawmidi_info_get_subdevices_count(rawMidiInfo))
					{
						// We found a MIDI Input device. Format its name in the caller's buffer
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
	snd_rawmidi_t		*midiInHandle;

	{
	char					cardName[64];

	// Did user supply a MIDI Input? If not, we need to find one	
	if (argc < 2)
	{
		find_midi_in(&cardName[0]);
		if (!cardName[0])
		{
			printf("Can't find a MIDI Input to receive from!\n");
			return 1;
		}
	}

	// Use the one he supplied
	else
		sprintf(&cardName[0], "hw:%s", argv[1]);

	// Open input MIDI device
	if ((err = snd_rawmidi_open(&midiInHandle, 0, &cardName[0], 0)) < 0)
	{
		printf("Can't open MIDI Input %s: %s\n", &cardName[0], snd_strerror(err));
		return 1;
	}

	// Trap when user presses CTRL-C
	signal(SIGINT, sighandler);

	printf("Receiving MIDI on %s...\nPress CTRL-C to abort.\n", &cardName[0]);
	}

	{
	register unsigned char	runningStatus, dataBytesSoFar;
	unsigned char				inputByte;
	
	dataBytesSoFar = StopFlag = 0;
	runningStatus = 0xF0;

	for (;;)
	{
		// Read the next incoming MIDI byte. NOTE: Since we didn't specify
		// the NONBLOCK flag to snd_rawmidi_open, this call will block until
		// a byte arrives at MIDI in (or there's an error, or user aborts)
		if (snd_rawmidi_read(midiInHandle, &inputByte, 1) != 1) break;

		if (StopFlag) break;

		// Resolve running status ourselves. Is this a status byte?
		if (inputByte >= 0x80)
		{
			// Save the new running status (minus any channel)
			runningStatus = inputByte & 0xF0;

			// Start a new line since this is obviously a new message. We're
			// going to put each MIDI message on its own line. The exception
			// will be with SysEx messages, which we'll arbitrarily limit to 40 bytes
			// per line
			if (dataBytesSoFar > 1) printf("\n");

			// No data bytes yet received for this message
			dataBytesSoFar = 1;
		}

		// Display the byte
		printf("%02x ", inputByte);

		{
		register unsigned char	count;
		
		// Figure out how many bytes this message should have (so we
		// can display each message on its own line)
		switch (runningStatus)
		{
			case 0xC0:
			case 0xD0:
				count = 2;
				break;

			case 0xF0:
				count = 41;
				break;
					
			default:
				count = 3;
		}

		// The end of the message?
		if (dataBytesSoFar++ >= count)
		{
			printf("\n");
			
			// Assume next message will be running status, and therefore the
			// status byte is implicit (ie, won't be received again)
			dataBytesSoFar = 1;
		}
		}
	}
	}

	// Close the MIDI Input
	snd_rawmidi_close(midiInHandle);

	return 0;
}
