/*
 * $Id$
 * 
 * Licence: see LICENSE
 * author: mac@calmar.ws Marco Candrian
 * 
 * Modified for use in Conky by Brenden Matthews
 *
 * Description:
 * scanning from top to bottom on a mbox
 * The output as follows:
 * F: FROM_LENGHT S: SUBJECT_LENGHT
 *
 * Usage: ${mboxscan [-n <number of messages to print>] 
 *                   [-fw <from width>] [-sw <subject width>] 
 *                   [-t <minumum delay in sec> "mbox" }
 */

#include "conky.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mboxscan.h"

#define FROM_WIDTH 10
#define SUBJECT_WIDTH 22
#define PRINT_MAILS 5
#define TIME_DELAY 5

struct ring_list {
	char *from;
	char *subject;
	struct ring_list *previous;
	struct ring_list *next;
};

static time_t last_ctime;	/* needed for mutt at least */
static time_t last_mtime;	/* not sure what to test: testing both now */
static double last_update;

static int args_ok = 0;
static int from_width;
static int subject_width;
static int print_mails;
static int time_delay;

/*
 * I don't know what to use: TEXT_BUFFER_SIZE or text_buffer_size
 * text_buffer_size is the maximum output in chars
 * TEXT_BUFFER_SIZE actually something like a allowed size
 * for things in the config, below 'TEXT'. Or what is more probably
 * max_user_text. Anyway, I used TEXT_BUFFER_SIZE for not 'output' things here
 * -- calmar
 *
 *  To clarify, TEXT_BUFFER_SIZE is used for buffers of fixed size, and 
 *  text_buffer_size is used for buffers which can change in size.
 *  text_buffer_size is just defined as TEXT_BUFFER_SIZE to start,
 *  so its okay for most things, however if something is allocated
 *  with text_buffer_size and then text_buffer_size changes but
 *  the array doesn't, you might have some issues if you are using
 *  text_buffer_size to determine the size of the array.
 *  -- brenden
 */

static char mbox_mail_spool[TEXT_BUFFER_SIZE];

void mbox_scan(char *args, char *output, size_t max_len)
{
	int i, u, flag;
	int force_rescan = 0;
	char buf[text_buffer_size];
	struct stat statbuf;

	/* output was set to 1 after malloc'ing in conky.c */
	/* -> beeing able to test it here for catching SIGUSR1 */
	if (output[0] == 1) {
		force_rescan = 1;
		output[0] = '\0';
	}

	if (!args_ok || force_rescan) {

		char *substr = strstr(args, "-n");
		if (substr) {
			if (sscanf(substr, "-n %i", &print_mails) != 1) {
				print_mails = PRINT_MAILS;
			}
		} else {
			print_mails = PRINT_MAILS;
		}
		if (print_mails < 1)
			print_mails = 1;

		substr = strstr(args, "-t");
		if (substr) {
			if (sscanf(substr, "-t %i", &time_delay) != 1) {
				time_delay = TIME_DELAY;
			}
		} else {
			time_delay = TIME_DELAY;
		}

		substr = strstr(args, "-fw");
		if (substr) {
			if (sscanf(substr, "-fw %i", &from_width) != 1) {
				from_width = FROM_WIDTH;
			}
		} else {
			from_width = FROM_WIDTH;
		}

		substr = strstr(args, "-sw");
		if (substr) {
			if (sscanf(substr, "-sw %i", &subject_width) != 1) {
				subject_width = SUBJECT_WIDTH;
			}
		} else {
			subject_width = SUBJECT_WIDTH;
		}
		/* encapsulated with "'s find first occurrence of " */
		if (args[strlen(args) - 1] == '"') {
			strncpy(mbox_mail_spool, args, TEXT_BUFFER_SIZE);
			char *start = strchr(mbox_mail_spool, '"') + 1;
			start[(long)(strrchr(mbox_mail_spool, '"') - start)] = '\0';
			strncpy(mbox_mail_spool, start, TEXT_BUFFER_SIZE);
		} else {
			char *copy_args = strdup(args);
			char *tmp = strtok(copy_args, " ");
			char *start = tmp;
			while (tmp) {
				tmp = strtok(NULL, " ");
				if (tmp) {
					start = tmp;
				}
			}
			strncpy(mbox_mail_spool, start, TEXT_BUFFER_SIZE);
			free(copy_args);
		}
		if (strlen(mbox_mail_spool) < 1) {
			CRIT_ERR("Usage: ${mboxscan [-n <number of messages to print>] [-fw <from width>] [-sw <subject width>] [-t <delay in sec> mbox}");
		}

		/* allowing $MAIL in the config */
		if (!strcmp(mbox_mail_spool, "$MAIL")) {
			strcpy(mbox_mail_spool, current_mail_spool);
		}

		if (stat(mbox_mail_spool, &statbuf)) {
			CRIT_ERR("can't stat %s: %s", mbox_mail_spool, strerror(errno));
		}
		args_ok = 1;	/* args-computing necessary only once */
	}

	/* if time_delay not yet reached, then return */
	if (current_update_time - last_update < time_delay && !force_rescan)
		return;

	last_update = current_update_time;

	/* mbox still exists? and get stat-infos */
	if (stat(mbox_mail_spool, &statbuf)) {
		ERR("can't stat %s: %s", mbox_mail_spool, strerror(errno));
		output[0] = '\0';	/* delete any output */
		return;
	}

	/* modification time has not changed, so skip scanning the box */
	if (statbuf.st_ctime == last_ctime && statbuf.st_mtime == last_mtime && !force_rescan) {
		return;
	}

	last_ctime = statbuf.st_ctime;
	last_mtime = statbuf.st_mtime;

	/* build up double-linked ring-list to hold data, while scanning down * the mbox */
	struct ring_list *curr = 0, *prev = 0, *startlist = 0;

	for (i = 0; i < print_mails; i++) {
		curr = (struct ring_list *)malloc(sizeof(struct ring_list));
		curr->from = (char *)malloc(sizeof(char[from_width + 1]));
		curr->subject = (char *)malloc(sizeof(char[subject_width + 1]));
		curr->from[0] = '\0';
		curr->subject[0] = '\0';

		if (i == 0)
			startlist = curr;
		if (i > 0) {
			curr->previous = prev;
			prev->next = curr;
		}
		prev = curr;
	}

	/* connect end to start for an endless loop-ring */
	startlist->previous = curr;
	curr->next = startlist;

	/* mbox */
	FILE *fp;

	fp = fopen(mbox_mail_spool, "r");
	if (!fp) {
		return;
	}

	flag = 1;		/* first find a "From " to set it to 0 for header-sarchings */
	while (!feof(fp)) {
		if (fgets(buf, text_buffer_size, fp) == NULL)
			break;

		if (strncmp(buf, "From ", 5) == 0) {
			curr = curr->next;

			/* skip until \n */
			while (strchr(buf, '\n') == NULL && !feof(fp))
				fgets(buf, text_buffer_size, fp);

			flag = 0;	/* in the headers now */
			continue;
		}

		if (flag == 1) {	/* in the body, so skip */
			continue;
		}

		if (buf[0] == '\n') {
			/* beyond the headers now (empty line), skip until \n */
			/* then search for new mail ("From ") */

			while (strchr(buf, '\n') == NULL && !feof(fp))
				fgets(buf, text_buffer_size, fp);
			flag = 1;	/* in the body now */
			continue;
		}

		if ((strncmp(buf, "X-Status: ", 10) == 0)
		    || (strncmp(buf, "Status: R", 9) == 0)) {

			/* Mail was read or something, so skip that message */
			flag = 1;	/* search for next From */
			curr->subject[0] = '\0';
			curr->from[0] = '\0';
			curr = curr->previous;	/* (will get current again on new * 'From ' finding) */
			/* Skip until \n */
			while (strchr(buf, '\n') == NULL && !feof(fp))
				fgets(buf, text_buffer_size, fp);
			continue;
		}

		/* that covers ^From: and ^from: ^From:<tab> */
		if (strncmp(buf + 1, "rom:", 4) == 0) {

			i = 0;
			u = 6;	/* no "From: " string needed, so skip */
			while (1) {

				if (buf[u] == '"') {	/* no quotes around names */
					u++;
					continue;
				}

				if (buf[u] == '<' && i > 1) {	/* some are: From: * <foo@bar.com> */

					curr->from[i] = '\0';
					/* skip until \n */
					while (strchr(buf, '\n') == NULL && !feof(fp))
						fgets(buf, text_buffer_size, fp);
					break;
				}

				if (buf[u] == '\n') {
					curr->from[i] = '\0';
					break;
				}

				if (buf[u] == '\0') {
					curr->from[i] = '\0';
					break;
				}

				if (i >= from_width) {
					curr->from[i] = '\0';
					/* skip until \n */
					while (strchr(buf, '\n') == NULL && !feof(fp))
						fgets(buf, text_buffer_size, fp);
					break;
				}

				/* nothing special so just set it */
				curr->from[i++] = buf[u++];
			}
		}

		/* that covers ^Subject: and ^subject: and ^Subjec:<tab> */
		if (strncmp(buf + 1, "ubject:", 7) == 0) {

			i = 0;
			u = 9;	/* no "Subject: " string needed, so skip */
			while (1) {

				if (buf[u] == '\n') {
					curr->subject[i] = '\0';
					break;
				}
				if (buf[u] == '\0') {
					curr->subject[i] = '\0';
					break;
				}
				if (i >= subject_width) {
					curr->subject[i] = '\0';

					/* skip until \n */
					while (strchr(buf, '\n') == NULL && !feof(fp))
						fgets(buf, text_buffer_size, fp);
					break;
				}

				/* nothing special so just set it */
				curr->subject[i++] = buf[u++];
			}
		}

	}

	fclose(fp);

	output[0] = '\0';
	struct ring_list *tmp;
	i = print_mails;
	while (i) {
		if (curr->from[0] != '\0') {
			if (i != print_mails) {
				snprintf(buf, text_buffer_size, "\nF: %-*s S: %-*s", from_width, curr->from, subject_width, curr->subject);
			} else {	/* first time - no \n in front */
				snprintf(buf, text_buffer_size, "F: %-*s S: %-*s", from_width, curr->from, subject_width, curr->subject);
			}

		} else {
			snprintf(buf, text_buffer_size, "\n");
		}
		strncat(output, buf, max_len - strlen(output));

		tmp = curr;
		curr = curr->previous;
		free(tmp->from);
		free(tmp->subject);
		free(tmp);

		i--;
	}
}