#include <assert.h>
#include <sys/stat.h>
#include <libgen.h>
#include <bits/getopt_core.h>
#include <ctype.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include <malloc.h>
#ifdef __linux__
	#include <unistd.h>

int enableANSI() {
	return 0;
}

#elif _WIN32
	#include <windows.h>
	#define usleep(X) Sleep(X/1000)

int enableANSI() {
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Unable to get console handle\n");
        return 1;
    }
	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode)) {
        fprintf(stderr, "Error: Unable to get console mode\n");
        return 1;
    }
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode)) {
        fprintf(stderr, "Error: Unable to set console mode\n");
        return 1;
    }
	return 0;
}

#endif

#define USERID_LEN 10

struct Post {
	char id[10];
	char user[USERID_LEN];
	char userName[25];
	char service[15];
	char title[70];
	char published[20];
};

struct Post* postList;
int postListSize = 0;
int postListPos = 0;

int notANumber(char* str) {
	int i = 0;
	int isBlank = 1;
	while (str[i] != '\0') {
		isBlank = 0;
		if (str[i] < 0x30 || str[i] > 0x39) return 1;
		i++;
	}
	return isBlank;
}

int getNextKeyValue(
		char* str, int start, 
		char** keyOut, int keySize, 
		char** valueOut, int valueSize) {
	assert(valueSize > 6);
	int strLen = strlen(str);
	//printf("Parsing {%s} from index %i.", str, start);
	int keyPos = 0, valuePos = 0;
	enum {
		REGULAR,
		ESCAPED
	} charState;
	enum {
		SEEK,
		KEY,
		SEPARATOR,
		VALUE,
		ENDOFVALUE,
		SEEKEND
	} state;
	state = SEEK;
	charState = REGULAR;
	int i;
	for (i = 0; i < strLen-start && state != ENDOFVALUE; i++) {
		switch (state) {
		case SEEK:
			if (str[start+i] == '"') state = KEY;
			break;
		case KEY:
			switch (charState) {
				case REGULAR:
					if (str[start+i] == '"') {
						state = SEPARATOR;
					}	
					else if (str[start+i] == '\\') charState = ESCAPED;
					else {
						if (keyPos < keySize - 1) {
							(*keyOut)[keyPos] = str[start+i];
							keyPos++;
						}
						else {
							fprintf(stderr,"\033[91mKey length exceeded!\033[m");
							return -1;
						}
					}
					break;
				case ESCAPED:
					if (keyPos < keySize - 1) {
						(*keyOut)[keyPos] = str[start+i];
						keyPos++;
					}
					else {
						fprintf(stderr, "\033[91mKey length exceeded!\033[m");
						return -1;
					}
					charState = REGULAR;
					break;
			}
			break;
		case SEPARATOR:
			if (str[start+i] == '"') {
				//printf("[EOS]");
				state = VALUE;
				//printf("Ending key at index %i", keyPos);
				//fflush(stdout);
				(*keyOut)[keyPos] = '\0';
			}
			else if (str[start+i] != ':' && str[start+i] != ' ') {
				//printf("\nUnknown Symbols found in seperator at index %i!\n", start+i);
				(*valueOut)[0] = '\0';
				(*keyOut)[0] = '\0';
				//fflush(stdout);
				return -1;
			}
			//putchar('S');
			//fflush(stdout);
			break;
		case VALUE:
			//putchar('V');
			//fflush(stdout);
			switch (charState) {
				case REGULAR:
					if (str[start+i] == '"') state = ENDOFVALUE;
					else {
						if (str[start+i] == '\\') charState = ESCAPED;
						if (valuePos < valueSize - 1) {
							(*valueOut)[valuePos] = str[start+i];
							valuePos++;
						}
						else {
							(*valueOut)[valueSize-2] = '.';
							(*valueOut)[valueSize-3] = '.';
							(*valueOut)[valueSize-4] = '.';
							if ((*valueOut)[valueSize-5] == '\\' && (*valueOut)[valueSize-6] != '\\')
								(*valueOut)[valueSize-5] = '.';
							state = SEEKEND;
						}
					}
					break;
				case ESCAPED:
					if (valuePos < valueSize - 1) {
						(*valueOut)[valuePos] = str[start+i];
						valuePos++;
					}
					else {
						(*valueOut)[valueSize-2] = '.';
						(*valueOut)[valueSize-3] = '.';
						(*valueOut)[valueSize-4] = '.';
						state = SEEKEND;
					}
					charState = REGULAR;
					break;
			}
			break;
		case SEEKEND:
			switch (charState) {
				case REGULAR:
					if (str[start+i] == '"') state = ENDOFVALUE;
					if (str[start+i] == '\\') charState = ESCAPED;
					break;
				case ESCAPED:
					break;
			}
			break;
		default:
			fprintf(stderr, "\033[1;91mInvalid branch(\033[;40101m%i\033[1;91m) reached!\033[m", state);
			exit(1);
		}
	}
	
	if (state != ENDOFVALUE && state != VALUE && state != SEEKEND) {
		//printf("Invalid state(%i) on loop exit", state);
		return -1;
	}
	
	if (keyPos >= keySize || valuePos >= valueSize) {
		fprintf(stderr, "\033[1;91mUncaught buffer overload in keyValuePairParsing.\033[m\n");
		exit(1);
	}

	(*keyOut)[keyPos] = '\0';
	(*valueOut)[valuePos] = '\0';
	return start+i;
}


int stringEncode(char* str, int strLen) {
	int i;
	for (i=0; i < strLen; i++) if (str[i] == ' ') str[i] = '+';
	return 0;
}

int getCount(FILE* pageFile) {
	int count = -1;
	fscanf(pageFile, "{\"count\":%d", &count);
	return count;
}

int printProgress(int prog, int total) {
	if (total == 0) {
		printf("\033[91mNo Pages!\033[m");
		return -1;
	}
	char bar[51];
	int points, i;
	points = prog * 50 / total;
	if (points > 50) points = 50;
	for (i = 0; i < points; i++) bar[i] = '#';
	for (;i<50;i++) bar[i] = '-';
	bar[50] = '\0';
	printf("\n%s\033[107;30m[%i/%i]\033[m", bar, prog, total);
	return 0;
}

int getPost(FILE* pagefile, char** outStr) {
	int depth = 0;
	int postSize = 10;
	int pos = 0;
	enum {
		NO,
		YES
	} inComment, escaped;
	inComment = NO;
	escaped = NO;
	char c;
	char* post = malloc(sizeof(char)*10);
	while((c = getc(pagefile))) {
		if (c == -1) {
			*outStr = post;
			fprintf(stderr, "\033[1;91mError while seeking.\033[m");
			return 1;
		}
		if (c == '}' || c == -1) {
			*outStr = post;
			return -1;
		}
		if (c == '{') {
			depth++;
			break;
		}
	}
	while((c = getc(pagefile))) {
		if (c == '"' && escaped == NO) {
			if (inComment == NO) inComment = YES;
			else inComment = NO;
		}
		if (c == '\\' && escaped == NO) escaped = YES;
		else escaped = NO;
		if (c == -1) {
			*outStr = post;
			fprintf(stderr, "\033[1;91mError while parsing.\033[m");
			return 1;
		}
		if (c == '{' && inComment == NO) depth++;
		if (c == '}' && inComment == NO) {
			depth--;
			if (depth == 0) goto endofpost;
		}
		// Add char to string
		if (pos == postSize - 1) {
			post = realloc(post, postSize*2);
			if (post == NULL) {
				fprintf(stderr, "\033[1;91mReallocation in getPost failed!\033[m");
				exit(-1);
			}
			postSize = postSize * 2;
		}
		post[pos] = c;
		pos++;
	}
	*outStr = post;
	return -1;
endofpost:
	post[pos] = '\0';
	*outStr = post;
	//printf("%s\n", post);
	return 0;
}

int savePost(struct Post post) {
	if (postListPos >= postListSize) {
		postList = realloc(postList, sizeof(struct Post)*(postListSize+50));
		if (postList == NULL) {
			fprintf(stderr, "\033[1;91mFailed to allocate more space for postList!\033[m");
			exit(1);
		}
		postListSize += 50;
	}
	postList[postListPos] = post;
	postListPos++;

	return 0;
}

void printPost(struct Post post) {
	if (notANumber(post.user)) printf("\033[91m");
	printf("\nID: %s, USER: %s, NAME: %s, SERVICE: %s, DATE: %s, TITLE: %s\033[m", post.id, post.user, post.userName, post.service, post.published, post.title);
	fflush(stdout);
}

void printAllPosts() {
	printf("Printing %i posts:\n", postListPos);
	
	for (int i = 0; i < postListPos; i++) {
		if (i > 0 && strcmp(postList[i].user, postList[i-1].user) != 0) putchar('\n');
		printPost(postList[i]);
	}
}

int checkFilter(struct Post post, char filterTerm[]) {
	for(int i = 0; post.title[i]; i++){
		post.title[i] = tolower(post.title[i]);
	}
	for(int i = 0; filterTerm[i]; i++){
		filterTerm[i] = tolower(filterTerm[i]);
	}
	printf("\nChecking [%s] against [%s];", filterTerm, post.title);
	return ((strstr(post.title, filterTerm) != NULL) ? 1 : 0);
}

int processPost(char* str, char filterTerm[]) {
	int postValid = 1;
	struct Post post;
	post.id[0] = '\0';
	post.service[0] = '\0';
	post.title[0] = '\0';
	post.user[0] = '\0';
	post.userName[0] = '\0';
	post.published[0] = '\0';
	int pos = 0;
	char* key = malloc(sizeof(char)*20);
	char* value = malloc(sizeof(char)*70);
	if (key == NULL || value == NULL) {
		fprintf(stderr, "Key or Value allocation failed!");
		exit(1);
	}
	while (pos != -1) {
		pos = getNextKeyValue(str, pos, &key, 20, &value, 70);
		if (pos == -1) break;
		//printf("[\"%s\":\"%s\";\"%i\"]", key, value, pos);
		if (strcmp(key, "id") == 0) {
			snprintf(post.id, 10, "%s", value);
		}
		if (strcmp(key, "user") == 0) {
			snprintf(post.user, USERID_LEN, "%s", value);
		}
		if (strcmp(key, "service") == 0) snprintf(post.service, 15, "%s", value);
		if (strcmp(key, "title") == 0) snprintf(post.title, 70, "%s", value);
		if (strcmp(key, "published") == 0) snprintf(post.published, 20, "%s", value);
	}
	free(key);
	free(value);
	if (notANumber(post.id)) postValid = 0;
	if (notANumber(post.user)) postValid = 0;
	if (checkFilter(post, filterTerm) && postValid) savePost(post);
	else printf("\033[1;91mPOST REJECTED\033[m\n");
	printPost(post);
	return 0;
}

int comparePosts(const void* pv1, const void* pv2) {
	struct Post *p1 = (struct Post*)pv1;
	struct Post *p2 = (struct Post*)pv2;
	int r;
	if ((r = strcmp(p1->user, p2->user))) {
		return r;
	} else {
		return 0-strcmp(p1->published, p2->published);
	}	
}

size_t readUntil(char **buffer, size_t* bufferLen, char deliminator, FILE* inputFile) {
	char ch;
	char *outStr;
	*bufferLen = 0;
	outStr = malloc(0);
	while ((ch = getc(inputFile))) {
		if (ch == deliminator || ch == -1) break;
		void* tmpPtr = realloc(outStr, sizeof(char)*(++(*bufferLen)));
		if (tmpPtr == NULL) {
			free(outStr);
			return -1;
		} else {
			outStr = tmpPtr;
		}
		outStr[(*bufferLen)-1] = ch;
	}
	void* tmpPtr = realloc(outStr, sizeof(char)*((*bufferLen)+1));
	if (tmpPtr == NULL) {
		free(outStr);
		return -1;
	} else {
		outStr = tmpPtr;
	}
	outStr[*bufferLen] = '\0';
	*buffer = outStr;
	return *bufferLen;
}

int findUsernames() {
	FILE* userFile;
	char lastName[25];	
	CURL* curl_handler = curl_easy_init();
	int failed, failed_page, failed_count = 0;
	for (int i = 0; i < postListPos; i++) {
loopstart:
		failed = 0;
		failed_page = 0;
		if (i == 0 || strcmp(postList[i].user, postList[i-1].user) != 0) {
			userFile = fopen("user.txt", "w+");
			if (userFile) {
				curl_easy_setopt(curl_handler, CURLOPT_WRITEDATA, userFile);
				char userURLstart[64] = "https://kemono.su/api/v1/";
				static char userURLmid[] = "/user/";
				static char userURLend[] = "/profile";
				strcat(userURLstart, postList[i].service);
				strcat(userURLstart, userURLmid);
				strcat(userURLstart, postList[i].user);
				strcat(userURLstart, userURLend);
				printf("\nSetting user URL to: %s", userURLstart);
				fflush(stdout);
				curl_easy_setopt(curl_handler, CURLOPT_URL, userURLstart);
				curl_easy_setopt(curl_handler, CURLOPT_VERBOSE, 0);
				curl_easy_setopt(curl_handler, CURLOPT_SSL_SESSIONID_CACHE, 0);
				CURLcode res = curl_easy_perform(curl_handler);
				if (res != CURLE_OK) {
					fprintf(stderr, "\033[1;91mCURL FAILED due to: %s\033[m", curl_easy_strerror(res));
				}
				rewind(userFile);
				char* userPageBuffer = NULL;
				size_t userPageLen;
				size_t bytes_read = readUntil(&userPageBuffer, &userPageLen, '\0', userFile);
				if (bytes_read == -1) {
					fprintf(stderr, "\033[1;91muserFile to buffer read failed!\033[m");
					exit(1);
				}
				int pos = 0;
				char* key = malloc(sizeof(char)*20);
				char* value = malloc(sizeof(char)*70);
				while (pos != -1) {
					value[0] = '\0';
					pos = getNextKeyValue(userPageBuffer, pos, &key, 20, &value, 70);
					if (strcmp(key, "name") == 0) {
						snprintf(lastName, 25, "%s", value);
						break;
					}
					if (strcmp(key, "error") == 0 || pos == -1) {
						fprintf(stderr, "\033[91mFailed to get name of user #%i. Error: %s Repeating...\033[m\n", i, (pos == -1 ? "" : value));
						if (!strcmp(value, "Creator not found.")) {
							printf("\033[92mCancel. Continue.\033[m");
							break;
						}
						failed = 1;
						lastName[0] = '\0'; 
						usleep(3000000);
					}
				}
				free(key);
				free(value);
				free(userPageBuffer);
				fclose(userFile);
			} else {
				fprintf(stderr, "\033[1;91mFailed to open userFile on user #%i in findUsernames()!\033[m", i);
				failed_page = 1;
				failed_count++;
			}
		}
		snprintf(postList[i].userName, 25, "%s", lastName);
		if (failed_count > 5 && failed_page) exit(1);
		if (failed || failed_page) {
			usleep(1000);
			goto loopstart;
		}
		failed_count = 0;
	}
	curl_easy_cleanup(curl_handler);
	return 0;
}

void outputJson(FILE* jsonFile) {
	fprintf(jsonFile, "[");
	char lastUser[USERID_LEN];
	int firstEntry = 0;
	for (int i = 0; i < postListPos; i++) {
		firstEntry = 0;
		if (i == 0) {
			snprintf(lastUser, USERID_LEN, "%s", postList[i].user);
			fprintf(jsonFile, "{\"id\":\"%s\",\"name\":\"%s\",\"entries\":[", lastUser, postList[i].userName);
			firstEntry = 1;
		}
		else if (strcmp(lastUser, postList[i].user) != 0) {
			snprintf(lastUser, USERID_LEN, "%s", postList[i].user);
			fprintf(jsonFile, "]},{\"id\":\"%s\",\"name\":\"%s\",\"entries\":[", lastUser, postList[i].userName);
			firstEntry = 1;
		}
		if (!firstEntry) fprintf(jsonFile, ",");
		fprintf(jsonFile,
				"{\"id\":\"%s\",\"published\":\"%s\",\"service\":\"%s\",\"title\":\"%s\",\"link\":\"https://kemono.su/%s/user/%s/post/%s\"}", 
				postList[i].id, postList[i].published, postList[i].service, postList[i].title, postList[i].service, postList[i].user, postList[i].id);
	}
	if (postListPos != 0) fprintf(jsonFile, "]}");
	fprintf(jsonFile, "]");
}

int initSavesFolder(int useCustom, char customPath[]) {
	char exe_path[PATH_MAX];
	if (useCustom == 0) {
		ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
		if (len == -1) {
			perror("readlink");
			return 1;
		}
		exe_path[len] = '\0';
		dirname(exe_path);
		char target_path[PATH_MAX];
		snprintf(target_path, sizeof(target_path), "%s/saves", exe_path);
		int mkdirResult = mkdir(target_path, 0777);
	} else {
		int mkdirResult = mkdir(customPath, 0777);
	}
	return 0;
}

int outputSave(int useCustomSavesFolder, char customSavesFolder[], char searchTerm[], char filterTerm[]) {
	char target_path[PATH_MAX];
	if (useCustomSavesFolder == 0) {
		char exe_path[PATH_MAX];
		ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
		if (len == -1) {
			perror("readlink");
			return 1;
		}
		exe_path[len] = '\0';
		dirname(exe_path);
		snprintf(target_path, sizeof(target_path), "%s/saves/%s - [%s].json", exe_path, searchTerm, filterTerm);
	} else {
		snprintf(target_path, sizeof(target_path), "%s/saves/%s - [%s].json", customSavesFolder, searchTerm, filterTerm);
	}
	FILE* jsonFile = fopen(target_path, "w");
	if (!jsonFile) {
		printf("Failed to open external json file.");
		exit(1);
	}
	outputJson(jsonFile);
	fclose(jsonFile);
	return 0;
}

int checkSaveExistance(int useCustomSavesFolder, char customSavesFolder[], char searchTerm[], char filterTerm[]) {
	char target_path[PATH_MAX];
	if (useCustomSavesFolder == 0) {
		char exe_path[PATH_MAX];
		ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
		if (len == -1) {
			perror("readlink");
			return 1;
		}
		exe_path[len] = '\0';
		dirname(exe_path);
		snprintf(target_path, sizeof(target_path), "%s/saves/%s - [%s].json", exe_path, searchTerm, filterTerm);
	} else {
		snprintf(target_path, sizeof(target_path), "%s/saves/%s - [%s].json", customSavesFolder, searchTerm, filterTerm);
	}
	return (access(target_path, F_OK) == 0) ? 1 : 0;
}

int main(int argc, char* argv[]) {
	if (enableANSI()) return 1;
	int useExternalJsonFile = 0;
	int bypassPostLimit = 0, pageDelay = 0, useCustomSavesFolder = 0, skipSaveCheck = 0;
	char externalJson[260];
	char customSavesFolder[PATH_MAX];
	int opt;

	while ((opt = getopt(argc, argv, "uj:d:f:s")) != -1) {
		switch (opt) {
			case 'u':
				bypassPostLimit = 1;
				break;
			case 'j':
				useExternalJsonFile = 1;
				snprintf(externalJson, 260, "%s", optarg);
				break;
			case 'd':
				pageDelay = atoi(optarg);
				break;
			case 'f':
				useCustomSavesFolder = 1;
				snprintf(customSavesFolder, sizeof(customSavesFolder), "%s", optarg);
				break;
			case 's':
				skipSaveCheck = 1;
				break;
		}
	}

	initSavesFolder(useCustomSavesFolder, customSavesFolder);

	static char urlbase[130] = "https://kemono.su/api/v1/posts?q=";
	char searchTerm[40];
	char filterTerm[40];
	FILE* pagefile = NULL;
	int runLoop = 1;
	int postVar = 0;
	int maxPost = 1;
	postList = malloc(sizeof(struct Post)*50);
	if (postList == NULL) {
		fprintf(stderr, "Failed to allocate space for memoryList.");
		exit(1);
	}
	postListSize = 50;

	printf("KEMONO SEARCH");
	printf("\nEnter Search term: ");
	fgets(searchTerm, sizeof(searchTerm), stdin);
	searchTerm[strcspn(searchTerm, "\n")] = 0;
	printf("Enter Filter term: ");
	fgets(filterTerm, sizeof(filterTerm), stdin);
	filterTerm[strcspn(filterTerm, "\n")] = 0;
	stringEncode(searchTerm, strlen(searchTerm));

	if((!skipSaveCheck) & checkSaveExistance(useCustomSavesFolder, customSavesFolder, searchTerm, filterTerm)) {
		fprintf(stderr, "This search is already saved! Rerun program with -s option to skip this check.\n");
		return 1;
	}

	strcat(urlbase, searchTerm);
	printf("\nFull URL base: %s", urlbase);
	
	CURL* easy_handler = curl_easy_init();
	while (postVar < maxPost) {
		char urlfull[140];
		char countstring[10];
		snprintf(countstring, 9, "&o=%d", postVar);
		strcpy(urlfull, urlbase);
		strcat(urlfull, countstring);
		curl_easy_setopt(easy_handler, CURLOPT_URL, urlfull);
		curl_easy_setopt(easy_handler, CURLOPT_VERBOSE, 0);
		curl_easy_setopt(easy_handler, CURLOPT_SSL_SESSIONID_CACHE, 0);
		pagefile = fopen("page.txt", "w+");
		if (pagefile) {
			curl_easy_setopt(easy_handler, CURLOPT_WRITEDATA, pagefile);
			usleep(pageDelay*1000);
			CURLcode res = curl_easy_perform(easy_handler);
			if (res != CURLE_OK) {
				fprintf(stderr, "CURL FAILED");
			}
		} else {
			fprintf(stderr, "Failed to open pagefile!");
			exit(-1);
		}
		rewind(pagefile);
		if (postVar == 0) {
			int count = getCount(pagefile);
			if (count == -1) {
				fprintf(stderr, "Failed to get page - No count value!");
			}
			printf("Post Count: %d", count);
			if (count > 5500 && !bypassPostLimit) {
				printf("\nNarrow search to below 5000 results to avoid DDoS protection!\n");
				exit(1);
			}
			maxPost = count;
		} else {
			getCount(pagefile);
		}
		int postReturn;
		while(1) {
			char* postStr;
			postReturn = getPost(pagefile, &postStr);
			if (postReturn != 0){
				free(postStr);
				break;
			}
			processPost(postStr, filterTerm);
			free(postStr);
		}

		if (postReturn != 1) postVar += 50;
		else{
			printf("repeating page because of DDoS protection.");
			char ch;
			rewind(pagefile);
			while ((ch = fgetc(pagefile)) != EOF) {
				putchar(ch);
			}
			curl_easy_cleanup(easy_handler);
			easy_handler = curl_easy_init();
			usleep(3000000);
		}
		printProgress(postVar, maxPost);
		fflush(stdout);
		fclose(pagefile);
		//usleep(1000000);
	}
	curl_easy_cleanup(easy_handler);


	printf("Sorting %i posts.", postListPos);
	qsort(postList, postListPos, sizeof(struct Post), comparePosts);
	
	findUsernames();

	printAllPosts();
	FILE* jsonFile;
	if (useExternalJsonFile) {
		jsonFile = fopen(externalJson, "w");
		if (!jsonFile) {
			printf("Failed to open external json file.");
			exit(1);
		}
		outputJson(jsonFile);
		fclose(jsonFile);
	} else {
		outputJson(stdout);
	}

	outputSave(useCustomSavesFolder, customSavesFolder, searchTerm, filterTerm);

	return 0;
}
