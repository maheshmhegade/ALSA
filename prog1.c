// countcards.c
// Counts how many sound cards ALSA finds in the system.
//
// Compile as:
// gcc -o countcards countcards.c -lasound

#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>

int main(int argc, char **argv)
{
   register int  err;
   int           cardNum, totalCards;

   // No cards found yet
   totalCards = 0;

   // Start with first card
   cardNum = -1;

   for (;;)
   {
      // Get next sound card's card number. When "cardNum" == -1, then ALSA
      // fetches the first card
      if ((err = snd_card_next(&cardNum)) < 0)
      {
         printf("Can't get the next card number: %s\n", snd_strerror(err));
         break;
      }

      // No more cards? ALSA sets "cardNum" to -1 if so
      if (cardNum < 0) break;

      // Another card found, so bump the count
      ++totalCards;
   }

   printf("ALSA found %i cards\n", totalCards);

   // ALSA allocates some mem to load its config file when we call
   // snd_card_next. Now that we're done getting the info, let's tell ALSA
   // to unload the info and free up that mem
   snd_config_update_free_global();
}
