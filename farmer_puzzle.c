/*
 * farmer_puzzle.c
 * Author: Max Greenwald
 * Date: October 16, 2018
 *
 * A solver for a mastermind-like puzzle that takes a secret code and attempts
 * to guess that code in the minimum number of guesses. Run with '-h' to see
 * additional options.
 *
 */


/***************/
/*  Libraries  */
/***************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <pthread.h>


/***************/
/*  Constants  */
/***************/

// Define helpful constant values and functions
#define TRUE 1
#define FALSE 0
#define min(a, b) (a < b ? a : b)


/************************/
/*  Argument Constants  */
/************************/

// Define global program arguments. These are used as constants
// throughout the program and should never be mutateted.
int DIGITS = 10;
int CODE_LENGTH = 5;
int THREADS = 5;
int INITIAL_GUESS = 112;


/**************/
/*  Typedefs  */
/**************/

// A "code", or array of digits (integers) of length CODE_LENGTH. Digits in the
// code can be between 0 (inclusive) and DIGITS (non-inclusive).
typedef int *Code;

// A "possible code". An element of a linked list that stores all of the
// remaining codes that are still eligible to be scored and tested.
typedef struct CodeNode {
  Code code;
  struct CodeNode *next; // The next possible code in the linked list.
} CodeNode;

// An "offer" of goats and chickens made by the farmer.
typedef struct Offer {
    int goats;
    int chickens;
} Offer;

// A "partition" of the remaining codes for an individual thread to score.
// Starts at index of possible_codes and ends after size possible codes.
typedef struct Partition {
  int index; // The start index in the possible_codes array
  int size; // The number of codes to score.
} Partition;


/**********************/
/*  Global Variables  */
/**********************/

// Array of all the codes in (DIGITS^CODE_LENGTH). Also holds the elements
// of the linked list possible_codes. Readonly from workers
CodeNode *all_codes;

// Linked list of the remaining possible codes. CodeNodes are stored in the
// array all_codes, and the linked list next refs are modified as the
// possible_codes decrease. Readonly from workers.
CodeNode *possible_codes = NULL;

// The best next guess and its 'score' (the minimum number of possible codes
// this guess will remove if it is played). These two variables many only be
// modified by acquiring next_guess_lock.
Code next_guess = NULL;
int next_guess_score = 0;
pthread_mutex_t next_guess_lock;


/***********************/
/*  Helpers Functions  */
/***********************/

/**
    Print a code to stdout. If the DIGIT base is greater than 10, include
    dashes between each digit.

    @param Code The code to be printed.
*/
void print_code(Code code) {
  for (int i = 0; i < CODE_LENGTH; i++) {
    printf("%i", code[i]);
    if (DIGITS > 10 && i != CODE_LENGTH - 1) {
      printf("-");
    }
  }
}

/**
    Transform an integer into a code in base DIGITS. Allocate space for the code
    and return it.

    @param int The int to be transformed into a code in base 10
    @return Code the new code in base DIGITS
*/
Code int_to_code(int num) {
  Code code = malloc(CODE_LENGTH * sizeof(int));
  int index = CODE_LENGTH - 1;
  for (int i = 0; i < CODE_LENGTH; i++) {
    code[i] = 0;
  }
  while (num != 0) {
    code[index] = num % DIGITS;
    num /= DIGITS;
    index--;
  }
  return code;
}

/**
    Check a guess against a code and load the return offer with the
    corresponding goats and chickens.

    @param Offer* The location where the return_offer should be placed
    @param Code The guess to check against the code
    @param Code The code against which the guess is checked
*/
void make_guess(Offer *return_offer, Code guess, Code code) {
  // Initialize offer counters
  int goats = 0;
  int chickens = 0;

  // NOTE: these digits are chicken-only counters. Goats are counted immediately
  int *guess_digits = calloc(DIGITS, sizeof(int));
  int *code_digits = calloc(DIGITS, sizeof(int));

  // Check every index in the guess and code for a match (goat)
  // If there is no match, update the digit counts in the corresponding array
  for (int i = 0; i < CODE_LENGTH; i++) {
    if (guess[i] == code[i]) {
      goats++;
    } else {
      guess_digits[guess[i]]++;
      code_digits[code[i]]++;
    }
  }

  // Get the number of chickens by summing the minimum of the digit counts
  // for each digit. Each non-overlapping match will add one chicken.
  for (int i = 0; i < DIGITS; i++) {
    chickens += min(guess_digits[i], code_digits[i]);
  }

  // Free digit arrays
  free(guess_digits);
  free(code_digits);

  // Load the offer into the return_offer
  return_offer->goats = goats;
  return_offer->chickens = chickens;
}

/**
    Remove all of the impossible codes from possible_codes. Iterate over the
    linked list and remove both the current guess as well as all codes which
    do not result in the same offer as the current guess when run against
    the current guess code.

    @param Code The current guess
    @param Offer The offer resulting from making the current guess against
      the real code
    @return int The updated count of the codes in possible_codes
*/
int remove_impossible_codes(Code guess, Offer offer) {
  // Initialize possible code counter and a reusable possible_offer
  int possible_code_count = 0;
  Offer possible_offer = {0, 0};

  // Initialize pointers for iteration over linked list
  CodeNode *prev = NULL;
  CodeNode *curr = possible_codes;

  // Iterate over possible_codes, removing codes that do not give the same offer
  // when checked with guess.
  while (curr != NULL) {
    // Get the current code and make a guess.
    Code curr_code = curr->code;
    make_guess(&possible_offer, guess, curr_code);

    // Check if the code is also the current guess. If so, we will
    // remove it from possible codes
    int code_is_guess = TRUE;
    for (int i = 0; i < CODE_LENGTH; i++) {
      if (guess[i] != curr_code[i]) {
        code_is_guess = FALSE;
        break;
      }
    }

    // If the code is the current guess or if the offer it generated is not the
    // same as the offer resulting from the current guess, remove the code
    // from possible_codes.
    if (possible_offer.goats    != offer.goats    ||
        possible_offer.chickens != offer.chickens ||
        code_is_guess
       ) {

      // If the current element is at the start of possible_codes,
      // move the start of possible codes to the next element.
      if (curr == possible_codes) {
        possible_codes = curr->next;

      // If this is not the first element, set the previous node's next to the
      // current node's next
      } else {
        prev->next = curr->next;
      }

    // If the offers are the same, keep the code in possible_codes.
    // Countthe code and iterate
    } else {
      possible_code_count++;
      prev = curr;
    }

    // Set the current node to the next node.
    curr = curr->next;
  }

  // Always make sure the last node in the list points to NULL!
  prev->next = NULL;

  return possible_code_count;
}


/***************/
/*  Functions  */
/***************/

/**
    Run a worker that acts on a partition of the possible_codes and finds the
    possible code in its partition that, if chosen as the guess, would at worst
    remove the most other possible codes from the selection process. This is
    accomplished by assuming that each code in the partition is the actual code,
    getting the count of all the other possible codes that, if guess, would,
    in the same offer, and then chosing the code (that we assumed is the actual
    code) that has the highest minimum removal of other codes. Store the result
    in next_guess and next_guess_score, which must be accessed with a mutex lock

    @param void* A dereferenced Partition* that contains the partition info
      for this worker
*/
void *worker(void *input) {
  // Cast the partition so we can use it
  Partition *partition = (Partition *)input;

  // Intialize counters and storage for the score
  int codes_checked = 0;
  int score = 0;
  int size = partition->size;

  // Setup pointers for the best guess and get the current guess
  Offer offer;
  CodeNode *curr_code;
  CodeNode *best_guess = NULL;
  CodeNode *curr_guess = all_codes + partition->index;

  // We will reuse this so we don't want to recalculate it later
  int CODE_COUNT = pow(DIGITS, CODE_LENGTH);

  // Store the count of the offers with that goat + chicken count
  // at the index (goats * CODE_LENGTH) + chickens, which can't be longer
  // than CODE_LENGTH ^ 2
  int *offer_results = calloc(CODE_LENGTH * CODE_LENGTH, sizeof(int));

  // Loop over all potential guseses in this partition
  while (codes_checked < size) {
    if (curr_guess == NULL) break;

    // Set the current code to the beginning of the possible_codes linked list
    // and get its actual code
    curr_code = possible_codes;

    // Loop over all codes in possible_codes (as curr_code)
    while (curr_code != NULL) {

      // For each curr_code, check the result of making a guess with the
      // curr_guess, assuming curr_code is the actual code.
      make_guess(&offer, curr_guess->code, curr_code->code);

      // Store the count of the same offers in offer_results
      int index = (offer.goats * CODE_LENGTH) + offer.chickens;
      offer_results[index] += 1;

      curr_code = curr_code->next;
    }

    // Get the count of the min number of codes removed from possible_codes
    int min_removed = CODE_COUNT;
    for (int i = 0; i < CODE_LENGTH * CODE_LENGTH; i++) {
      // Get the count and clear offer_results
      int result = offer_results[i];
      offer_results[i] = 0;

      // The minimum cannot be 0
      if (result == 0) {
        continue;
      }

      min_removed = min(result, min_removed);
    }

    // If the min_removed is better than our current score, update it.
    if (min_removed > score) {
      score = min_removed;
      best_guess = curr_guess;
    }

    // Setup for next iteration
    curr_guess = curr_guess->next;
    codes_checked++;
  }

  // Free used space
  free(offer_results);

  // Lock mutex and insert our result if it is the best score
  pthread_mutex_lock(&next_guess_lock);
  if (score > next_guess_score) {
    next_guess = best_guess->code;
    next_guess_score = score;
  }
  pthread_mutex_unlock(&next_guess_lock);

  return NULL;
}

/**
    The entry point of the puzzle. Parse command line arguments, initialize
    global variables, read the "secret" code from stdin, and run the main
    puzzle loop, starting workers when necessary.

    Use the '-h' option to view usage information.
*/
int main(int argc, char **argv) {

  // Get the command line arguments.
  int arg;
  while ((arg = getopt (argc, argv, "hd:l:t:g:")) != -1) {
    switch (arg) {
      case 'h':
        printf("Options:\n");
        printf("  -d <digits> Number of possible digits (base, Default: 10)\n");
        printf("  -l <length> Length of the code (Default: 5) \n");
        printf("  -t <threads> (Default 5)\n");
        printf("  -g <initial-guess> (Default: 112)\n");
        return 0;
      case 'd':
        DIGITS = (int)strtol(optarg, (char **)NULL, 10);
        if (DIGITS <= 0) {
          printf("Invalid digits: must be at least one.\n");
          return 1;
        }
        break;
      case 'l':
        CODE_LENGTH = (int)strtol(optarg, (char **)NULL, 10);
        if (CODE_LENGTH <= 0) {
          printf("Invalid code length: must be at least one.\n");
          return 1;
        }
        break;
      case 't':
        THREADS = (int)strtol(optarg, (char **)NULL, 10);
        if (THREADS <= 0) {
          printf("Invalid thread count: must be at least one.\n");
          return 1;
        }
        break;
      case 'g':
        INITIAL_GUESS = (int)strtol(optarg, (char **)NULL, 10);
        break;
      default:
        break;
    }
  }

  // Initialie next guess lock
  pthread_mutex_init(&next_guess_lock, NULL);

  // Get the code from stdin (1 line with a newline at the end)
  size_t size;
  char *code_line = NULL;
  if (getline(&code_line, &size, stdin) == -1) {
    printf("Could not get code input. Exiting.");
    return 1;
  }

  // Convert the code from an int to a string. The input will be assumed
  // to be in base DIGITS
  Code code = int_to_code((int)strtol(code_line, (char **)NULL, DIGITS));

  // Initialize possible codes
  int CODE_COUNT = pow(DIGITS, CODE_LENGTH);
  all_codes = malloc(sizeof(CodeNode) * CODE_COUNT);

  // Set each code's code and next pointer
  for (int i = 0; i < CODE_COUNT; i++) {
    all_codes[i].code = int_to_code(i);
    if (i == CODE_COUNT - 1) {
      // For the last element, set the 'next' to NULL
       all_codes[i].next = NULL;
    } else {
       all_codes[i].next = all_codes + i + 1;
    }
  }

  // Initially, the first possible code is the first element of the list.
  possible_codes = all_codes;

  // Setup the loop variables. Start guess at the initial guess.
  int guesses = 0;
  Code guess = int_to_code(INITIAL_GUESS);
  Offer offer = {0, 0};

  // Run the main loop! Iterate until we have an offer that has the same
  // number of goats as digits.
  while(TRUE) {

    // Make the guess!
    make_guess(&offer, guess, code);
    guesses++;

    // Print out guess and results of offer to stdout
    printf("\nGuess: ");
    print_code(guess);
    printf("\nNumber of guesses: %i", guesses);
    printf("\nGoats: %i\nChickens: %i\n", offer.goats, offer.chickens);

    // Check if we got the code correct. If so, leave the loop
    if (offer.goats == CODE_LENGTH) {
      break;
    }

    // Remove all the codes in possible_codes that don't have
    // the same offer for this guess
    int possible_code_count = remove_impossible_codes(guess, offer);

    // Partition the remaining possible_codes so that each thread can work
    // on a portion of the remaining codes
    int codes_per_thread = possible_code_count / THREADS;
    Partition partitions[THREADS];

    // Initialize
    pthread_t threads[THREADS];
    CodeNode *curr_code = possible_codes;

    // Start all the threads
    for (int thread = 0; thread < THREADS; thread++) {
      // Set the start index for this thread
      partitions[thread].index = curr_code - all_codes;

      // Set the partition size for this thread
      partitions[thread].size =
        min(possible_code_count, (thread + 1) * codes_per_thread)
          - (thread * codes_per_thread);

      // Initialize the worker thread
      pthread_create(threads + thread, NULL, worker, (void *)(partitions + thread));

      // For all threads except the last one, walk along the linked list
      // possible_codes by codes_per_thread steps so that the next thread
      // starts on the next unused code
      if (thread < THREADS - 1) {
        for (int i = 0; i < codes_per_thread; i++) {
          curr_code = curr_code->next;
        }
      }
    }

    // Join all of the started threads
    for (int thread = 0; thread < THREADS; thread++) {
      pthread_join(threads[thread], NULL);
    }

    // At this point, next_guess_score should be set. If it is 0, there was
    // no best guess found, and we choose the first possible_code as our
    // next guess
    if (next_guess_score == 0) {
      guess = possible_codes->code;

    // If there was a best next_guess found, use it!
    } else {
      guess = next_guess;
      next_guess_score = 0;
    }
  }

  // Cleanup and exit
  for (int i = 0; i < CODE_COUNT; i++) {
    free(all_codes[i].code);
  }

  free(all_codes);
}
