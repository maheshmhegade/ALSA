// This example lists all of the digital audio input and output
// hardware (ie, ALSA pcm) ports upon all sound cards
// in the system.
//
// Compile as:
// gcc -o listpcm listpcm.c -lasound

#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>





static unsigned char VerboseFlag = 0;





/****************** list_subdevices() *********************
 * Lists the audio outputs or inputs upon the specified
 * device (number) of the specified card (number).
 *
 * audioHandle =	Handle to sound card.
 * cardNum =		Card number of sound card.
 * devNum =			Device number (on the card).
 * type =			SND_PCM_STREAM_PLAYBACK to list outputs,
 *						or SND_PCM_STREAM_CAPTURE for inputs.
 */
 
 static void list_subdevices(snd_ctl_t *audioHandle, int cardNum, int devNum, int type)
{
	snd_pcm_info_t		*pcmInfo;
	register int		subDevCount, i, err;
	const char			*str;

	str = (type == SND_PCM_STREAM_PLAYBACK ? "Output" : "Input");

	// To get some info about the subdevices of this PCM device (on the card), we need a
	// snd_pcm_info_t, so let's allocate one on the stack
	snd_pcm_info_alloca(&pcmInfo);
	memset(pcmInfo, 0, snd_pcm_info_sizeof());

	// Tell ALSA which device (number) we want info about
	snd_pcm_info_set_device(pcmInfo, devNum);

	// Get info on the PCM out or in section of this device
	snd_pcm_info_set_stream(pcmInfo, type);

	i = -1;
	subDevCount = 1;
		
	// More subdevices?
	while (++i < subDevCount)
	{
		// Tell ALSA to fill in our snd_pcm_info_t with info on this subdevice
		snd_pcm_info_set_subdevice(pcmInfo, i);
		if ((err = snd_ctl_pcm_info(audioHandle, pcmInfo)) < 0)
		{
			printf("Can't get info for audio %s subdevice 'hw:%i,%i,%i': %s\n", str, cardNum, devNum, i, snd_strerror(err));
			continue;
		}

		// Print out how many subdevices (once only)
		if (!i)
		{
			subDevCount = snd_pcm_info_get_subdevices_count(pcmInfo);
			printf("\n ------ %i %s subdevices (%i available)\n", subDevCount, str, snd_pcm_info_get_subdevices_avail(pcmInfo));
			if (!VerboseFlag)
			{
				printf("id = '%s'\n", snd_pcm_info_get_id(pcmInfo));
				printf("name = '%s'\n", snd_pcm_info_get_name(pcmInfo));
			}
		}

		// NOTE: If there's only one subdevice, then the subdevice number is immaterial, and can be
		// omitted when you pass the name to snd_pcm_open()
		printf((subDevCount > 1 ? "\n    Audio %s 'hw:%i,%i,%i'\n" : "\n    Audio %s 'hw:%i,%i'\n") , str, cardNum, devNum, i);

		if (VerboseFlag)
		{
			printf("    id = '%s'\n", snd_pcm_info_get_id(pcmInfo));
			printf("    name = '%s'\n", snd_pcm_info_get_name(pcmInfo));
		}
		printf("    subname = '%s'\n", snd_pcm_info_get_subdevice_name(pcmInfo));
	}
}





int main(int argc, char** argv)
{
	register int			err;
	int						cardNum;

	if (argc > 1) VerboseFlag = 1;

	// Start with first card
	cardNum = -1;

	for (;;)
	{
		snd_ctl_t			*audioHandle;

		// Get next sound card's card number. When "cardNum" == -1, then ALSA
		// fetches the first card
		if ((err = snd_card_next(&cardNum)) < 0)
		{
			printf("Can't get the next card number: %s\n", snd_strerror(err));
			break;
		}

		// No more cards? ALSA sets "cardNum" to -1 if so
		if (cardNum < 0) break;

		// Open this card's control interface. We specify only the card number -- not any device nor sub-device too
		{
		char					str[64];

		sprintf(str, "hw:%i", cardNum);
		if ((err = snd_ctl_open(&audioHandle, str, 0)) < 0)
		{
			printf("Can't open card %i: %s\n", cardNum, snd_strerror(err));
			continue;
		}
		}

		{
		int							devNum;
		register unsigned char	doneOnce;

		doneOnce = 0;

		// Start with the first device on this card, and list all digital audio devices
		devNum = -1;
		for (;;)
		{
			// Get the number of the next audio device on this card
			if ((err = snd_ctl_pcm_next_device(audioHandle, &devNum)) < 0)
			{
				printf("Can't get next audio device: %s\n", snd_strerror(err));
				break;
			}

			// No more audio devices on this card? ALSA sets "devNum" to -1 if so.
			// NOTE: It's possible that this sound card may have no audio devices on it
			// at all, for example if it's only a MIDI card
			if (devNum < 0) break;

			// List some info about the card itself, such as its ID (name), now that we've
			// discovered that it contains one or more audio devices. Do this once
			// only
			if (!doneOnce)
			{
				snd_ctl_card_info_t	*cardInfo;

				// Do the code below once only
				doneOnce = 1;

				// We need to get a snd_ctl_card_info_t. Just alloc it on the stack
				snd_ctl_card_info_alloca(&cardInfo);

				// Tell ALSA to fill in our snd_ctl_card_info_t with info about this card
				if ((err = snd_ctl_card_info(audioHandle, cardInfo)) < 0)
					printf("Can't get info for card %i: %s\n", cardNum, snd_strerror(err));
				else
				{
					printf("\n\n==================================================================\n");
					printf("%s\n", snd_ctl_card_info_get_longname(cardInfo));
					printf("==================================================================\n");
					printf("id = '%s'\n", snd_ctl_card_info_get_id(cardInfo));
					printf("name = '%s'\n", snd_ctl_card_info_get_name(cardInfo));
					if (VerboseFlag)
					{
						printf("driver = '%s'\n", snd_ctl_card_info_get_driver(cardInfo));
						printf("mixername = '%s'\n", snd_ctl_card_info_get_mixername(cardInfo));
						printf("components = '%s'\n", snd_ctl_card_info_get_components(cardInfo));
					}
				}
			}

			// Display info about the Audio Output subdevices of this audio device
			list_subdevices(audioHandle, cardNum, devNum, SND_PCM_STREAM_PLAYBACK);			

			// Display info about the Audio Input subdevices of this audio device
			list_subdevices(audioHandle, cardNum, devNum, SND_PCM_STREAM_CAPTURE);			
		}
		}

		// Close the card's control interface after we're done listing its audio subdevices
		snd_ctl_close(audioHandle);
	}

	// ALSA allocates some mem to load its config file when we call some of the
	// above functions. Now that we're done getting the info, let's tell ALSA
	// to unload the info and free up that mem
	snd_config_update_free_global();

	return 0;
}

