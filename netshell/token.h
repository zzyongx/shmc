/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#ifndef _TOKEN_H_
#define _TOKEN_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct token_t {
	const char *value;
	size_t length;
};

int tokenize(char *command, struct token_t *token, size_t mtokens);

#ifdef __cplusplus
}
#endif

#endif
