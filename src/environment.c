#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assert.h"
#include "debug.h"
#include "dma.h"
#include "n64sys.h"
#include "scratch.h"

#define HEADER_CHECK_CODE_2  0x10000014
#define HEADER_FLAGS         0x10000038
#define HEADER_GAME_ID       0x1000003C

#define FLAG_HAS_ENV_STORAGE 2

static const char* env_marker = "ENV";

void __environment_init(void) {
	uint32_t game_id = io_read(HEADER_GAME_ID);

	if (!((game_id >> 24) == 'E' && ((game_id >> 16) & 0xFF) == 'D'))
		return;

	uint32_t flags = io_read(HEADER_FLAGS);
	if (!(flags & FLAG_HAS_ENV_STORAGE))
		return;

	uint32_t env_offset = io_read(HEADER_CHECK_CODE_2) + 0x10000000;

	uint32_t marker = io_read(env_offset);
	if (memcmp(env_marker, &marker, 4) != 0)
		return;

	uint32_t env_size = io_read(env_offset + 4);
	uint32_t aligned_size = (env_size + 8) & ~7;
	if (env_size == 0)
		return;

	char* env = scratch_malloc(aligned_size);
	data_cache_hit_invalidate(env, aligned_size);
	dma_read(env, (pi_addr_t) (env_offset + 8), aligned_size);
	env[env_size] = 0;

	char* name = NULL;

	for (int i = 0; i < env_size + 1;) {
		int len = strlen(&env[i]);
		if (len == 0) break;
		if (name == NULL) {
			name = &env[i];
		} else {
			setenv(name, &env[i], true);
			name = NULL;
		}
		i += len + 1;
	}

	scratch_free(env);
}
