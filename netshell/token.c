/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#include <token.h>

int tokenize(char *command, struct token_t *tokens, size_t mtokens)
{
	char *s, *e;
	size_t ntokens = 0;

    int esc = 0;

	for (s = e = command; ntokens < mtokens-1; ++e) {
        if (esc) {
            esc = 0; 
            continue;
        }

		if (*e == ' ') {
			if (s != e) {
				tokens[ntokens].value = s;
				tokens[ntokens].length = e - s;
				ntokens++;
				*e = '\0';
			}
			s = e + 1;
		} else if (*e == '\0') {
			if (s != e) {
				tokens[ntokens].value = s;
				tokens[ntokens].length = e - s;
				ntokens++;
			}
			break;
		} else if (*e == '\\') {
            esc = 1;         
        }
	}

	tokens[ntokens].value = (*e == '\0' ? NULL : e);
	tokens[ntokens].length = 0;
	ntokens++;

	return ntokens;
}

