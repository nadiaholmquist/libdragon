#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>


#define HEADER_CHECK_CODE_2  0x14
#define HEADER_FLAGS         0x38
#define HEADER_GAME_ID       0x3C

#define FLAG_HAS_ENV_STORAGE 2

const char* env_marker = "ENV";

void usage(void) {
}

bool check_flag(const char* arg, const char* shortFlag, const char* longFlag) {
	return !strcmp(arg, shortFlag) || !strcmp(arg, longFlag);
}

ssize_t parse_bytes(const char* arg) {
	size_t arg_len = strlen(arg);

	if(arg_len < 1) return -1;

	char* suffix;
	size_t size = strtol(arg, &suffix, 10);

	if (arg == suffix) return -1; // Invalid string
	if (arg_len == 1) return size;

	/* Multiply by the suffix magnitude */
	switch(suffix[0]) {
		case 'm':
		case 'M':
			size *= 1024;
		case 'k':
		case 'K':
			size *= 1024;
		case 'b':
		case 'B':
		case '\0':
			return size;
		default:
			return -1;
	}
}

void create_env_storage(FILE* rom, uint32_t capacity) {
	fseek(rom, 0, SEEK_END);
	while (ftell(rom) % 16 != 0)
		fputc(0, rom);

	uint32_t env_offset = ftell(rom);

	fwrite(env_marker, 1, 4, rom);
	capacity -= 8;
	uint32_t capacity_be = __builtin_bswap32(capacity);
	fwrite(&capacity_be, 1, 4, rom);

	while (capacity--) {
		fputc(0, rom);
	}

	fseek(rom, HEADER_FLAGS, SEEK_SET);
	uint8_t flags;
	fread(&flags, 1, 1, rom);
	flags |= FLAG_HAS_ENV_STORAGE;
	fseek(rom, HEADER_FLAGS, SEEK_SET);
	fwrite(&flags, 1, 1, rom);

	fseek(rom, HEADER_CHECK_CODE_2, SEEK_SET);
	env_offset = __builtin_bswap32(env_offset);
	fwrite(&env_offset, 1, 4, rom);
}

bool locate_environment(FILE* rom, uint32_t* offset, uint32_t* capacity) {
	size_t old_pos = ftell(rom);

	uint32_t env_offset;
	fseek(rom, HEADER_CHECK_CODE_2, SEEK_SET);
	fread(&env_offset, 1, 4, rom);
	env_offset = __builtin_bswap32(env_offset);
	fseek(rom, env_offset, SEEK_SET);

	char marker[4];
	fread(&marker, 1, 4, rom);
	if (memcmp(env_marker, marker, 4) != 0) {
		fprintf(stderr, "Invalid env marker in ROM.\n");
		fseek(rom, old_pos, SEEK_SET);
		return false;
	}

	uint32_t env_capacity;
	fread(&env_capacity, 1, 4, rom);
	env_capacity = __builtin_bswap32(env_capacity);
	if (env_capacity == 0) {
		fprintf(stderr, "ROM does not specify environment capacity.\n");
		fseek(rom, old_pos, SEEK_SET);
		return false;
	}

	*offset = env_offset;
	*capacity = env_capacity;

	fseek(rom, old_pos, SEEK_SET);

	return true;
}

bool set_environment(FILE* rom, const char** variables, size_t num_variables) {
	uint32_t offset, capacity;
	bool valid = locate_environment(rom, &offset, &capacity);
	if (!valid) return false;

	offset += 8;

	fseek(rom, offset, SEEK_SET);

	uint32_t needed = num_variables;
	for (int i = 0; i < num_variables; i++)
		needed += strlen(variables[i]);

	if (needed > capacity) {
		fprintf(stderr, "Environment can't fit in ROM, have capacity of %d bytes but would need %d.\n", capacity, needed);
	}

	for (int i = 0; i < num_variables; i++) {
		size_t var_len = strlen(variables[i]);
		int eq = -1;
		for (int j = 0; j < var_len; j++) {
			if (variables[i][j] == '=') {
				eq = j;
				break;
			}
		}
		fwrite(variables[i], eq, 1, rom);
		fputc(0, rom);
		fwrite(variables[i] + eq + 1, var_len - eq - 1, 1, rom);
		fputc(0, rom);
	}

	while (ftell(rom) % (offset + capacity) != 0)
		fputc(0, rom);

	return true;
}

#define OPERATION_CREATE 0
#define OPERATION_SET 1
#define OPERATION_DISPLAY 2

int main(int argc, char** argv) {
	if (argc < 2) {
		usage();
		return 1;
	}

	FILE* rom = NULL;
	size_t env_capacity = -1;
	size_t padding = 16384;

	size_t variables_capacity = 16;
	size_t num_variables = 0;
	const char** variables = calloc(variables_capacity, sizeof(char*));

	int operation = -1;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		const char* param = (i+1) >= argc ? NULL : argv[i+1];

		if (check_flag(arg, "-i", "--input")) {
			if (rom) {
				fprintf(stderr, "Only one ROM can be specified.");
				return 1;
			}

			if (param == NULL) {
				fprintf(stderr, "A parameter for -i is required.");
				return 1;
			}

			rom = fopen(param, "r+b");
			if (!rom) {
				perror(param);
				return errno;
			}
			i++;
			continue;
		} else if (check_flag(arg, "", "--create")) {
			operation = OPERATION_CREATE;
			continue;
		} else if (check_flag(arg, "", "--set")) {
			operation = OPERATION_SET;
			continue;
		} else if (check_flag(arg, "", "--display")) {
			operation = OPERATION_DISPLAY;
			continue;
		} else if (check_flag(arg, "-c", "--capacity")) {
			if (param == NULL) {
				fprintf(stderr, "A parameter for -c is required.");
				return 1;
			}

			size_t new_capacity = parse_bytes(param);
			if (new_capacity == -1) {
				fprintf(stderr, "Capacity must be a positive integer\n");
				return 1;
			}

			env_capacity = new_capacity;
			i++;
			continue;
		} else if (check_flag(arg, "-P", "--padding")) {
			if (param == NULL) {
				fprintf(stderr, "A parameter for -P is required.");
				return 1;
			}

			size_t new_padding = parse_bytes(param);
			if (new_padding == -1) {
				fprintf(stderr, "Padding must be a positive integer\n");
				return 1;
			}

			padding = new_padding;
			i++;
			continue;
		}

		size_t len = strlen(arg);
		int found = false;
		for (int i = 0; i < len; i++) {
			if (arg[i] == '=') {
				found = true;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, "Environment variables must be defined as NAME=VALUE\n");
			return 1;
		}

		if (num_variables + 1 >= variables_capacity) {
			variables_capacity += 16;
			variables = realloc(variables, variables_capacity * sizeof(char*));
		}
		variables[num_variables++] = arg;
	}

	/*for (int i = 0; i < num_variables; i++) {
		printf("%s\n", variables[i]);
	}*/

	if (!rom) {
		fprintf(stderr, "A ROM must be specified.\n");
		return 1;
	}

	bool is_homebrew = false;
	bool has_env_storage = false;

	char game_id[2];
	fseek(rom, HEADER_GAME_ID, SEEK_SET);
	fread(&game_id, 1, 2, rom);

	if (game_id[0] == 'E' && game_id[1] == 'D')
		is_homebrew = true;
	
	uint8_t flags = 0;
	fseek(rom, HEADER_FLAGS, SEEK_SET);
	fread(&flags, 1, 1, rom);

	if (is_homebrew && flags & 2)
		has_env_storage = true;

	if (operation == OPERATION_CREATE) {
		if (!is_homebrew) {
			fprintf(stderr, "Environment variable storage requires using the homebrew header.\n");
			return 1;
		}
		if (has_env_storage) {
			fprintf(stderr, "This ROM already has environment variable storage.\n");
		}

		if (env_capacity == -1) {
			fprintf(stderr, "A size for the environment variable storage must be specified.\n");
			return 1;
		}

		create_env_storage(rom, env_capacity);
		fseek(rom, 0, SEEK_END);
		while (ftell(rom) % padding != 0)
			fputc(0, rom);
	} else if (operation == OPERATION_SET) {
		if (!has_env_storage) {
			fprintf(stderr, "The specified ROM can't store environment variables.\n");
			return 1;
		}
		
		set_environment(rom, variables, num_variables);
	} else {
		fprintf(stderr, "One operation (--create, --set, or --display) must be specified.");
		return 1;
	}

	fclose(rom);

	return 0;
}
