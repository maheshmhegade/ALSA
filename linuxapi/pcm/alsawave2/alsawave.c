// A simple C example to play a mono or stereo, 16-bit 44KHz
// WAVE file using ALSA. This goes directly to the first
// audio card (ie, its first set of audio out jacks). It
// uses the memory-mapped mode of outputting waveform data (ie,
// we directly write waveform data into the card's internal
// buffer).
//
// Compile as so to create "alsawave":
// gcc -o alsawave alsawave.c -lasound
//
// Run it from a terminal, specifying the name of a WAVE file to play:
// ./alsawave MyWaveFile.wav

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Include the ALSA .H file that defines ALSA functions/data
#include <alsa/asoundlib.h>





#pragma pack (1)
/////////////////////// WAVE File Stuff /////////////////////
// An IFF file header looks like this
typedef struct _FILE_head
{
	unsigned char	ID[4];	// could be {'R', 'I', 'F', 'F'} or {'F', 'O', 'R', 'M'}
	unsigned int	Length;	// Length of subsequent file (including remainder of header). This is in
									// Intel reverse byte order if RIFF, Motorola format if FORM.
	unsigned char	Type[4];	// {'W', 'A', 'V', 'E'} or {'A', 'I', 'F', 'F'}
} FILE_head;


// An IFF chunk header looks like this
typedef struct _CHUNK_head
{
	unsigned char ID[4];	// 4 ascii chars that is the chunk ID
	unsigned int	Length;	// Length of subsequent data within this chunk. This is in Intel reverse byte
							// order if RIFF, Motorola format if FORM. Note: this doesn't include any
							// extra byte needed to pad the chunk out to an even size.
} CHUNK_head;

// WAVE fmt chunk
typedef struct _FORMAT {
	short				wFormatTag;
	unsigned short	wChannels;
	unsigned int	dwSamplesPerSec;
	unsigned int	dwAvgBytesPerSec;
	unsigned short	wBlockAlign;
	unsigned short	wBitsPerSample;
  // Note: there may be additional fields here, depending upon wFormatTag
} FORMAT;
#pragma pack()






// Size of the audio card hardware buffer. Here we want it
// set to 1024 16-bit sample points. This is relatively
// small in order to minimize latency. If you have trouble
// with underruns, you may need to increase this, and PERIODSIZE
// (trading off lower latency for more stability)
#define BUFFERSIZE	(2*1024)

// How many sample points the ALSA card plays before it calls
// our callback to fill some more of the audio card's hardware
// buffer. Here we want ALSA to call our callback after every
// 64 sample points have been played
#define PERIODSIZE	(2*64)

// Handle to ALSA (audio card's) playback port
snd_pcm_t				*PlaybackHandle;

// Handle to our callback thread
snd_async_handler_t	*CallbackHandle;

// Points to loaded WAVE file's data
short						*WavePtr;

// Size (in frames) of loaded WAVE file's data
unsigned int			WaveSize;

// Number of channels in the wave file
unsigned char			WaveChannels;

// How many frames we've already copied (from WavePtr to the audio card's buffer)
unsigned int			PlayPosition;

// The name of the ALSA port we output to. In this case, we're
// directly writing to hardware card 0,0 (ie, first set of audio
// outputs on the first audio card)
static const char		SoundCardPortName[] = "hw:0,0";

// For WAVE file loading
static const unsigned char Riff[4]	= { 'R', 'I', 'F', 'F' };
static const unsigned char Wave[4] = { 'W', 'A', 'V', 'E' };
static const unsigned char Fmt[4] = { 'f', 'm', 't', ' ' };
static const unsigned char Data[4] = { 'd', 'a', 't', 'a' };





/********************** compareID() *********************
 * Compares the passed ID str (ie, a ptr to 4 Ascii
 * bytes) with the ID at the passed ptr. Returns TRUE if
 * a match, FALSE if not.
 */

static unsigned char compareID(const unsigned char * id, unsigned char * ptr)
{
	register unsigned char i = 4;

	while (i--)
	{
		if ( *(id)++ != *(ptr)++ ) return(0);
	}
	return(1);
}





/********************** waveLoad() *********************
 * Loads a WAVE file.
 *
 * fn =			Filename to load.
 *
 * RETURNS: 0 if success, non-zero if not.
 *
 * NOTE: Sets the global "WavePtr" to an allocated buffer
 * containing the wave data, and "WaveSize" to the size
 * in sample points.
 */

static unsigned char waveLoad(const char *fn)
{
	const char				*message;
	FILE_head				head;
	register int			inHandle;

	if ((inHandle = open(fn, O_RDONLY)) == -1)
		message = "didn't open";

	// Read in IFF File header
	else
	{
		if (read(inHandle, &head, sizeof(FILE_head)) == sizeof(FILE_head))
		{
			// Is it a RIFF and WAVE?
			if (!compareID(&Riff[0], &head.ID[0]) || !compareID(&Wave[0], &head.Type[0]))
			{
				message = "is not a WAVE file";
				goto bad;
			}

			// Read in next chunk header
			while (read(inHandle, &head, sizeof(CHUNK_head)) == sizeof(CHUNK_head))
			{
				// ============================ Is it a fmt chunk? ===============================
				if (compareID(&Fmt[0], &head.ID[0]))
				{
					FORMAT	format;

					// Read in the remainder of chunk
					if (read(inHandle, &format.wFormatTag, sizeof(FORMAT)) != sizeof(FORMAT)) break;

					// Can't handle compressed WAVE files
					if (format.wFormatTag != 1)
					{
						message = "compressed WAVE not supported";
						goto bad;
					}

					// Only 16-bit allowed
					if (format.wBitsPerSample != 16)
					{
						message = "must be a 16-bit WAVE!";
						goto bad;
					}

					// Only 44100 sample rate allowed
					if (format.dwSamplesPerSec != 44100)
					{
						message = "rate must be 44.1 KHz";
						goto bad;
					}

					if (format.wChannels > 2)
					{
						message = "must be mono or stereo";
						goto bad;
					}

					WaveChannels = format.wChannels;
				}

				// ============================ Is it a data chunk? ===============================
				else if (compareID(&Data[0], &head.ID[0]))
				{
					// Size of wave data is head.Length. Allocate a buffer and read in the wave data
					if (!(WavePtr = (short *)malloc( head.Length)))
					{
						message = "won't fit in RAM";
						goto bad;
					}

					if (read(inHandle, WavePtr, head.Length) != head.Length)
					{
						free(WavePtr);
						break;
					}

					// size must be in terms of sample frames
					WaveSize = head.Length / 2;

					close(inHandle);
					return(0);
				}

				// ============================ Skip this chunk ===============================
				else
				{
					if (head.Length & 1) ++head.Length;  // If odd, round it up to account for pad byte
					lseek(inHandle, head.Length, SEEK_CUR);
				}
			}
		}

		message = "is a bad WAVE file";
bad:	close(inHandle);
	}

	printf("%s %s\n", fn, message);
	return(1);
}





/********************** audioRecovery() **********************
 * Called whenever we encounter an error in filling the sound
 * card's buffer with more audio data.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle".
 */

static int audioRecovery(register int err)
{
	// Under-run?	
	if (err == -EPIPE)
	{
		// NOTE: If you see these during playback, you'll have to increase
		// BUFFERSIZE/PERIODSIZE
		printf("underrun\n");
		if ((err = snd_pcm_prepare(PlaybackHandle)) >= 0)
good:		return(0);
		printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
	}

	// Audio suspended?
	else if (err == -ESTRPIPE)
	{
		printf("audio suspended\n");

		// Wait until the suspend flag is released
		while ((err = snd_pcm_resume(PlaybackHandle)) == -EAGAIN) sleep(1);
		if (err >= 0) goto good;
		if ((err = snd_pcm_prepare(PlaybackHandle)) >= 0) goto good;
		printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
	}

	return(err);
}





/********************** copy_wave_data() **********************
 * Copies more of our wave data to the current position of the
 * sound card's buffer.
 *
 * buffer =		Pointer to the head of the sound card buffer.
 * offset =		Offset to where we start copying data. This is
 *					in terms of frames, not bytes. For 16-bit stereo
 *					playback, then a frame = 2 16-bit words (ie,
 *					4 bytes).
 * numSamples = Number of frames we must copy.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle". A pointer to the wave data must be in
 * the global "WavePtr", and its size of "WaveSize". The
 * current playback position must be in "PlayPosition".
 */

static void copy_wave_data(const snd_pcm_channel_area_t *buffer, snd_pcm_uframes_t offset, snd_pcm_uframes_t numSamples)
{
	register short		*bufPtr;

	// Get the address of the audio card's interleaved buffer. Note: "offset" is in sample frames. Since we're
	// doing 16-bit stereo, there are two 16-bit sample pointer per frame. That means 4 bytes per
	// frame. So, to get the current byte offset within the sound card buffer, we multiply by 4
	bufPtr = (short *)(((unsigned char *)buffer[0].addr) + (offset * 4));

	// Copy as many sample points as we have wave data yet to be played, until we fill as many sample
	// frames as ALSA told us to fill. Also update "PlayPosition" global
	while (PlayPosition < WaveSize && numSamples)
	{
		register short	s16;

		s16 = WavePtr[PlayPosition++];
		*(bufPtr)++ = s16;

		// If mono, just copy the left channel to the right channel. Otherwise,
		// we have a separate sample point for the right channel
		if (WaveChannels == 1)
			*(bufPtr)++ = s16;
		else
			*(bufPtr)++ = WavePtr[PlayPosition++];

		--numSamples;
	}

	// Did we run out of wave data before we filled as many sample points as ALSA told us
	// to fill? (ie, we got to the end of the wave playback). If so, we have to fill the
	// remainder of the sound card buffer with 0. Remember that the sound card's interrupt
	// handler will also play PERIODSIZE frames. So if we don't fill its buffer to at least
	// that amount with "silence" (zero values), then we may hear garbage audio
	while (numSamples)
	{
		*(bufPtr)++ = 0;
		*(bufPtr)++ = 0;
		--numSamples;
	}
}





/********************** audio_callback() **********************
 * Called by ALSA whenever the sound card's buffer needs to be
 * filled with more audio data.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle". A pointer to the wave data must be in
 * the global "WavePtr", and its size of "WaveSize". The
 * current playback position must be in "PlayPosition".
 */

static void audio_callback(snd_async_handler_t *ahandler)
{
	register int						err;
	register unsigned char			first;
	snd_pcm_uframes_t					size;

restart:
	first = 0;
	while (1)
	{
		// Check state, and if there's an error, try to recover
		switch (snd_pcm_state(PlaybackHandle))
		{
			case SND_PCM_STATE_XRUN:
			{
				if ((err = audioRecovery(-EPIPE)))
				{
					printf("XRUN recovery failed: %s\n", snd_strerror(err));
out:				return;
				}

				first = 1;
				break;
			}

			case SND_PCM_STATE_SUSPENDED:
			{
				if ((err = audioRecovery(-ESTRPIPE)))
				{
					printf("SUSPEND recovery failed: %s\n", snd_strerror(err));
					goto out;
				}
			}
		}

		// Get how many frames the sound card needs us to copy to the sound
		// card's buffer. It should be at least PERIODSIZE, unless there's
		// an error
		if ((err = snd_pcm_avail_update(PlaybackHandle)) < 0)
		{
			if ((err = audioRecovery(err)))
			{
				printf("Avail update failed: %s\n", snd_strerror(err));
				goto out;
			}

			first = 1;
			continue;
		}

		if (err < PERIODSIZE)
		{
			if (first)
			{
				if ((err = snd_pcm_start(PlaybackHandle)) < 0)
				{
					printf("Start error: %s\n", snd_strerror(err));
					goto out;
				}

				goto restart;
			}

			break;
		}

		// Fill the audio buffer with PERIODSIZE of WAVE data
		size = PERIODSIZE;
		do
		{
			const snd_pcm_channel_area_t	*buffer;
			snd_pcm_uframes_t					offset, frames;

			// Get the pointer to the audio hardware's buffer where we need to copy WAVE data,
			// and the amount of bytes to copy. NOTE: If the buffer wraps, then the amount
			// of bytes to copy may be less than a full block (in which case "frames" will be
			// less than "size"
			frames = size;
			if ((err = snd_pcm_mmap_begin(PlaybackHandle, &buffer, &offset, &frames)) < 0)
			{
				if ((err = audioRecovery(err)) < 0)
				{
					printf("MMAP begin error: %s\n", snd_strerror(err));
					goto out;
				}

				goto first;
			}

			// Copy our wave data to the audio hardware's buffer. Pass the address of the
			// interleaved buffers. Note: "offset" is in sample frames. Since we're doing 16-bit stereo,
			// there are two 16-bit sample pointer per frame. That means 4 bytes per frame. So, to get
			// the current byte offset within the buffer, we multiply by 4
			copy_wave_data(buffer, offset, frames);

			// Commit the data
			if ((err = snd_pcm_mmap_commit(PlaybackHandle, offset, frames)) < 0 || (snd_pcm_uframes_t)err != frames)
			{
				if ((err = audioRecovery(err >= 0 ? -EPIPE : err)) < 0)
				{
					printf("MMAP commit error: %s\n", snd_strerror(err));
					goto out;
				}

first:		first = 1;
			}

			// Dec count of frames we still need to write to complete the block
			size -= frames;
		} while (size > 0);
	}
}





/********************** start_audio() **********************
 * Initially fills the sound card's buffer before playback,
 * then starts playback.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle". A pointer to the wave data must be in
 * the global "WavePtr", and its size of "WaveSize".
 */

static int start_audio(void)
{
	register int			err;
	int						count;
	snd_pcm_uframes_t		offset, frames, size;

	PlayPosition = 0;

	snd_pcm_prepare(PlaybackHandle);

	// Fill in at least the first PERIODSIZE block of the audio card's buffer (before we
	// start playback)
	for (count = 0; count < 2; count++)
	{
		const snd_pcm_channel_area_t	*buffer;

		size = PERIODSIZE;
		do
		{
			// Get how many frames the sound card needs us to copy
			while ((err = snd_pcm_avail_update(PlaybackHandle)) < 0)
			{
				if ((err = audioRecovery(err)))
				{
					printf("Avail update failed: %s\n", snd_strerror(err));
out:				return(err);
				}
			}
			frames = size;
			if ((err = snd_pcm_mmap_begin(PlaybackHandle, &buffer, &offset, &frames)) < 0)
			{
				if ((err = audioRecovery(err)))
				{
					printf("MMAP begin avail error: %s\n", snd_strerror(err));
					goto out;
				}
			}

			// Fill in the buffer
			copy_wave_data(buffer, offset, frames);

			// Commit the data
			if ((err = snd_pcm_mmap_commit(PlaybackHandle, offset, frames)) < 0 || (snd_pcm_uframes_t)err != frames)
			{
				if ((err = audioRecovery(err >= 0 ? -EPIPE : err)))
				{
					printf("MMAP commit error: %s\n", snd_strerror(err));
					goto out;
				}
			}

			// Dec count of frames we still need to write
			size -= frames;

		} while (size > 0 && frames);
	}

	// Start the playback
	if ((err = snd_pcm_start(PlaybackHandle)) < 0)
	{
		printf("Start error: %s\n", snd_strerror(err));
		goto out;
	}

	return(0);
}





/*********************** free_wave_data() *********************
 * Frees any wave data we loaded.
 *
 * NOTE: A pointer to the wave data be in the global
 * "WavePtr".
 */

static void free_wave_data(void)
{
	if (WavePtr) free(WavePtr);
	WavePtr = 0;
}





/********************* set_audio_hardware() *******************
 * This sets the audio card's hardware settings, such as sample
 * playback rate and bit resolution, to our desired settings.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle".
 *
 * RETURNS: 0 if success, or non-zero if error.
 */

static int set_audio_hardware(void)
{
	register int			err;
	snd_pcm_hw_params_t	*hw_params;

	// We need to get an ALSA "sound hardware struct" in order to change any of
	// the hardware settings of the sound card. We must ask ALSA to allocate
	// this struct for us. We can't declare one of them on our own. ALSA doesn't
	// allow us to know anything about what fields are inside one of these structs,
	// nor even what the sizeof the struct is. So the only way to get one of these
	// structs is to call this ALSA function to allocate one
	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
	{
		printf("Can't get sound hardware struct %s\n", snd_strerror(err));
bad1:	snd_pcm_close(PlaybackHandle);
		return(err);
	}

	// Fill in the sound hardware struct with the current hardware settings for
	// the audio card. We'll going to alter a few fields, but leave the rest of
	// the fields at their current values. So, we need to fill in all of the fields
	// to their current values. Calling this ALSA function does that
	if ((err = snd_pcm_hw_params_any(PlaybackHandle, hw_params)) < 0)
	{
		printf("Can't init sound hardware struct: %s\n", snd_strerror(err));
bad2:	snd_pcm_hw_params_free(hw_params);
		goto bad1;
	}

	// Our example WAVE file is 16-bit, so we want to set the hardware struct's "bit"
	// field to SND_PCM_FORMAT_S16_LE. NOTE: We can't directly set the field ourselves like this:
	// hw_params->bits = SND_PCM_FORMAT_S16_LE;
	// ALSA doesn't let us directly set any fields of the hardware struct. Instead, we've
	// got to call an ALSA function that does the above assignment to the field for us, and
	// we pass the value we want the field set to
	if ((err = snd_pcm_hw_params_set_format(PlaybackHandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
	{
		printf("Can't set 16-bit: %s\n", snd_strerror (err));
		goto bad2;
	}

	// Our example WAVE file is 44.1KHz, so we want to set the hardware struct's "rate"
	// field to 44100. We can't directly set the field ourselves. See note above
	if ((err = snd_pcm_hw_params_set_rate(PlaybackHandle, hw_params, 44100, 0)) < 0)
	{
		printf("Can't set sample rate: %s\n", snd_strerror(err));
		goto bad2;
	}

	// We want stereo playback, so we want to set the hardware struct's "channels"
	// field to 2
	if ((err = snd_pcm_hw_params_set_channels(PlaybackHandle, hw_params, 2)) < 0)
	{
		printf("Can't set stereo: %s\n", snd_strerror(err));
		goto bad2;
	}

	// We want to set the hardware struct's "interleaved" field. This means that,
	// when we copy the waveform data to the sound card's buffer, the first 16-bit
	// word is for the left channel, the second 16-bit word is for the right
	// channel, the third word is the next sample point for the left channel, etc.
	// WAVE files store their data interleaved, so this makes it easy for us to
	// copy the WAVE data verbatim to the sound card's buffer. Without interleaved,
	// we'd have to copy all the left channel data first, and then the right channel
	// data after the left channel's data
	if ((err = snd_pcm_hw_params_set_access(PlaybackHandle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0)
	{
		printf("Can't set interleaved playback: %s\n", snd_strerror(err));
		goto bad2;
	}

	// Tell ALSA to set the sound card's buffer size to BUFFERSIZE
	{
	snd_pcm_uframes_t buffer_size = BUFFERSIZE;

	if ((err = snd_pcm_hw_params_set_buffer_size_near(PlaybackHandle, hw_params, &buffer_size)) < 0)
	{
		printf("Can't set audio buffer size: %s\n", snd_strerror(err));
		goto bad2;
	}
	}

	// Tell ALSA to to consider PERIODSIZE frames to be the minimum amount of data that must
	// be copied to the sound card's buffer each time we copy more data (ie, the audio card
	// will always transfer PERIODSIZE frames to its DAC before its interrupt handler will
	// indicate for ALSA to feed it more data)
	{
	snd_pcm_uframes_t period_size = PERIODSIZE;

	if ((err = snd_pcm_hw_params_set_period_size_near(PlaybackHandle, hw_params, &period_size, 0)) < 0)
	{
		printf("Can't set audio period: %s\n", snd_strerror(err));
		goto bad2;
	}
	}

	// Ok, we've changed the fields of the hardware struct. Now we need to tell ALSA
	// to give all of these updated hardware settings back to the audio card, so that
	// the audio card can update its own settings to what we requested. The following
	// ALSA function does this. NOTE: If the hardware doesn't support the settings we
	// requested (for example, can't do 44100 rate), then we'll get an error. If we
	// were instead using the ALSA "PlugHw" device (instead of "hw"), then ALSA would
	// do some realtime "translating" of our data to accomodate the sound card's limits,
	// such as maybe downsampling/upsampling to whatever nearest rate the sound card
	// supports. The downside of this is that it introduces extra overhead during
	// playback which can result in overrun errors, anti-aliasing errors being heard
	// in the data, etc
	if ((err = snd_pcm_hw_params(PlaybackHandle, hw_params)) < 0)
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





/********************* set_audio_software() *******************
 * This sets the audio card's software settings, such as how
 * big we want its sound buffer to be, how often it notifies
 * us to refill its buffer, etc.
 *
 * NOTE: ALSA sound card's handle must be in the global
 * "PlaybackHandle".
 *
 * RETURNS: 0 if success, or non-zero if error.
 */

static int set_audio_software(void)
{
	register int			err;
	snd_pcm_sw_params_t	*sw_params;

	// We need to get an ALSA "sound software struct" in order to change any of
	// the software settings of the sound card. We must ask ALSA to allocate
	// this struct for us. We can't declare one of them on our own. This is the
	// same type of thing we did when setting the hardware settings above
	if ((err = snd_pcm_sw_params_malloc(&sw_params)) < 0)
	{
		printf("Can't get sound software struct: %s\n", snd_strerror(err));
bad1:	return(err);
	}

	// Fill in the software struct with the current software settings
	if ((err = snd_pcm_sw_params_current(PlaybackHandle, sw_params)) < 0)
	{
		printf("Can't init sound software struct: %s\n", snd_strerror(err));
bad2:	snd_pcm_sw_params_free(sw_params);
		goto bad1;
	}

	// Tell ALSA to call our callback whenever another PERIODSIZE frames of wave data can
	// be copied to the sound card's buffer
	if ((err = snd_pcm_sw_params_set_avail_min(PlaybackHandle, sw_params, PERIODSIZE)) < 0)
	{
		printf("Can't set audio period size: %s\n", snd_strerror(err));
		goto bad2;
	}

	// Tell ALSA to keep playback going as long as there's BUFFERSIZE - PERIODSIZE frames of
	// sample data yet to be played in the sound card's buffer. If things get so bogged down that
	// ALSA and our callback can't keep copying data to the sound card's buffer such that less than
	// BUFFERSIZE - PERIODSIZE frames are yet to be played, then ALSA will automatically stop
	// playback. This prevents us hearing potentially garbage audio if the sound card "gets ahead"
	// of us (ie, its interrupt handler feeds more samples to its DAC than we've actually copied to
	// its sound buffer)
	if ((err = snd_pcm_sw_params_set_start_threshold(PlaybackHandle, sw_params, BUFFERSIZE - PERIODSIZE)) < 0)
	{
		printf("Can't set start threshold: %s\n", snd_strerror(err));
		goto bad2;
	}

	// Let ALSA give these new settings to the audio card
	if ((err = snd_pcm_sw_params(PlaybackHandle, sw_params)) < 0)
	{
		printf("Can't set software params: %s\n", snd_strerror(err));
		goto bad2;
	}

	// Tell ALSA to call our audio_callback() function whenever we need to copy more
	// wave data to the sound card's buffer
	if ((err = snd_async_add_pcm_handler(&CallbackHandle, PlaybackHandle, audio_callback, 0)) < 0)
	{
		printf("Can't register sound callback: %s\n", snd_strerror(err));
		goto bad2;
	}

	// We no longer need the sound software struct
	snd_pcm_sw_params_free(sw_params);

	// Success
	return(0);
}





int main(int argc, char **argv)
{
	// No wave data loaded yet
	WavePtr = 0;

	if (argc < 2)
	{
		printf("You must supply the name of a 16-bit mono WAVE file to play\n");
	}

	// Load the wave file
	else if (!waveLoad(argv[1]))
	{
		register int		err;

		// Open audio card we wish to use for playback
		if ((err = snd_pcm_open(&PlaybackHandle, &SoundCardPortName[0], SND_PCM_STREAM_PLAYBACK, 0)) < 0)
			printf("Can't open audio %s: %s\n", &SoundCardPortName[0], snd_strerror(err));
		else
		{
			// Set the audio card's hardware parameters (sample rate, bit resolution, etc)
			if (!set_audio_hardware() &&

				// Set the audio card's software parameters (how we wish to fill its sound buffer, etc)
				!set_audio_software() &&

				// Initially fill in the sound card's hardware buffer with some data before we
				// start playback (so that we don't hear random audio garbage upon startup),
				// and then start the audio playback. ALSA will call our callback whenever it
				// needs us to copy more wave data to the sound card's buffer
				!start_audio())
			{
				// ALSA calls our callback on a separate thread, so our main thread has nothing
				// to do until playback is over. We'll just loop around waiting for our callback
				// to indicate that the wave file has been played to the end. That happens when
				// PlayPosition = the wave's total size
				while (PlayPosition < WaveSize) sleep(1000);
			}

			// Close sound card
			snd_pcm_close(PlaybackHandle);
		}
	}

	// Free the WAVE data
	free_wave_data();

	return(0);
}
