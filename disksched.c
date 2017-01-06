#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////
// Constants //
///////////////

#define SECTORSPERTRACK 200
#define TRACKSPERCYLINDER 8
#define SECTORSPERCYLINDER 1600
#define CYLINDERS 500000.0
#define RPM 10000
#define PHYSICALSECTORSIZE 512
#define LOGICALBLOCKSIZE 4096
#define TRACKTOTRACKSEEK 2.0
#define FULLSEEK 16.0
#define TRANSFERRATE 1

#define FILESIZE 128
#define ALGORITHMSIZE 4
#define LIMIT 32
#define MAXLINESIZE 1024
#define KEYWORDSIZE 32

//////////////////////
// Global Variables //
//////////////////////

int curCylinder = 0;
double curSector = 0;
double curTime = 0;

/////////////////
// Data Struct //
/////////////////

struct requestData {
	double arrivalTime;
	int LBN;
	int requestSize;
	int PSN;
	int cylinder;
	int surface;
	int sectorOffset;
	int D;
};

void printData(struct requestData *x) {
	printf("AT: %f Size: %d cylinder: %d D: %d\n", x->arrivalTime, x->requestSize, x->cylinder, x->D);
}


// Create Data Struct

struct requestData *createStruct(double arrivalTime, int LBN, int requestSize, int PSN, int cylinder, int surface, int sectorOffset, int D) {
	struct requestData *x = malloc(sizeof(struct requestData));

	x->arrivalTime = arrivalTime;
	x->LBN = LBN;
	x->requestSize = requestSize;
	x->PSN = PSN;
	x->cylinder = cylinder;
	x->surface = surface;
	x->sectorOffset = sectorOffset;
	x->D = D;

	return x;
}

////////////////////////
// Double Linked List //
////////////////////////

typedef struct node {
	struct node *next;
	struct node *prev;
	void *val;
} node;

typedef struct List {
	node *head;
	node *last;
	int count;
} List;

List *createList() {
	return calloc(1, sizeof(List));
}

// Insert a void pointer into the list

void insert(List *list, void *val) {
	node *x = calloc(1, sizeof(node));

	x->val = val;

	if (list->last == NULL) {
		list->head = x;
		list->last = x;
	}
	else {
		list->last->next = x;
		x->prev = list->last;
		list->last = x;
	}
	list->count+=1;
}

// Delete entire list

void deleteList(List *list) {
	node *y = list->head;
	for (; y != NULL; y = y->next) {
		if (y->prev) {	
			free(y->prev->val);	
			free(y->prev);
		}
	}
	if (list->last != NULL) free(list->last->val);
	free(list->last);
	free(list);

}

// Delete single node

void deleteNode(List *list, node *x) {
	if (list->count > 1) {
		if (x == list->head) {
			list->head = x->next;
			list->head->prev = NULL;
		}
		else if (x == list->last) {
			list->last = x->prev;
			list->last->next = NULL;
		}
		else {
			x->prev->next = x->next;
			x->next->prev = x->prev;
		}
	}
	else {
		list->head = NULL;
		list->last = NULL;
	}
	free(x->val);
	free(x);
	list->count -= 1;
}

void printList(List *list) {
	node *y = list->head;
	for (; y != NULL; y = y->next) {
		printData(y->val);

	}
}

////////////////////
// Best Seek Time //
////////////////////

int cmp(struct requestData *a, struct requestData *b) {
	if (a->D < b->D && a->arrivalTime <= curTime) return 1; // Checks if new entry is less than the best and if it has actually arrived
	else return 0;
}

// Absolute value for getting distance
int abs (int a) {
	if (a < 0) return a*(-1);
	else return a;
}

// Get the distance for each entry

void setDistance(struct requestData *x) {
	x->D = abs(x->cylinder - curCylinder);
}

// Finds best seek time and returns the node

node *find(List *list) {
	node *y = list->head;
	node *best = list->head;
	for (; y != NULL; y = y->next) {
		setDistance(y->val);
		if (cmp(y->val, best->val)) best = y; 

	}

	return best;
}

//////////////////////
// Service function //
//////////////////////

// Calculates all values for the request

void service(double arrivalTime, int LBN, int requestSize, int PSN, int cylinder, int surface, int sectorOffset, int D, FILE *fwrite) {
	double seekTime;
	double seekConstant = 0.000028;
	double sectorD;
	double latency;
	double transfer;
	double waitTime;

	// Seek Time
	if (curTime < arrivalTime) curTime = arrivalTime;
	curCylinder = cylinder;
	if (D == 0) seekTime = 0;
	else seekTime = seekConstant*D + TRACKTOTRACKSEEK;


	// Rotational Latency
	curSector = curSector + seekTime/.03;
	if (curSector >= 400) curSector = curSector - 400;
	else if (curSector >= 200) curSector = curSector - 200;

	sectorD = ((PSN%1600)%200) - curSector;
	if (sectorD < 0) sectorD = 200 + sectorD;

	curSector = curSector + sectorD + requestSize;
	if (curSector >= 400) curSector = curSector - 400;
	else if (curSector >= 200) curSector = curSector - 200;

	latency = (sectorD)*.03;

	// Transfer Time
	transfer = .003814697265625 *requestSize;

	waitTime = curTime - arrivalTime;
	curTime = curTime + ((seekTime + latency + transfer)/1000.0);

	// Output to file
	fprintf(fwrite, "%f %f %f %d %d %d %f\n", arrivalTime, curTime, waitTime, PSN + requestSize, cylinder, surface, curSector); 
}

// Specifically a function for SSTF, because of a different parameter

void serviceSSTF(struct requestData *x, FILE *fwrite) {
	service(x->arrivalTime, x->LBN, x->requestSize, x->PSN, x->cylinder, x->surface, x->sectorOffset, x->D, fwrite);
}

///////////////////
// FCFS Function //
///////////////////

void FCFS(char *inputFile, char *outputFile, int limit) {
	FILE *fread, *fwrite;
	char buffer2[MAXLINESIZE];
	char temp[MAXLINESIZE];
	char *q = NULL;
	char *saveptr;
	int line;

	double arrivalTime;
	int LBN;
	int requestSize;
	int D;
	int cylinder;
	int PSN;
	int surface;
	int sectorOffset;
	int xlimit = 0;

	// Write to out file
	fwrite = fopen(outputFile, "w");

	if (fwrite == NULL) {
		printf("\nError in output.\n");
		fclose(fwrite);
		exit(0);
	}


	// Read each line in and service the request
	fread = fopen(inputFile, "r");
	if (fread != NULL) {
		while( fgets(buffer2, MAXLINESIZE, fread) != NULL ) {
			strcpy(temp,buffer2);
			q = temp;
			line = 0;
			while ((q=strtok_r(q, " \r\n\0", &saveptr)) != NULL) {
				if (line == 0) arrivalTime = atof(q);
				else if (line == 1) LBN = atoi(q);
				else requestSize = atoi(q);
				q = NULL;
				line++;
			}

			// Calculate necessary parameters
			PSN = LBN*8;
			cylinder = PSN/1600;
			D = (LBN*8)/1600 - curCylinder;
			if (D < 0) D=D*(-1);
			surface = ((LBN*8)%1600)/200;
			sectorOffset = ((PSN%1600)%200) + requestSize;
	
			// Service the request		
			service(arrivalTime, LBN, requestSize, PSN, cylinder, surface, sectorOffset, D, fwrite);

			xlimit++;
			if (xlimit == limit) break; // If limit is set
		}
	} 
	else {

		printf("\nError in input.\n");
		fclose(fwrite);
		fclose(fread);
		exit(0);
	}
	fclose(fread);
	fclose(fwrite);
}

///////////////////
// SSTF Function //
///////////////////


void SSTF (char *inputFile, char *outputFile, int limit) {

	List *list = createList();
	FILE *fread, *fwrite;
	char buffer2[MAXLINESIZE];
	char temp[MAXLINESIZE];
	char *q = NULL;
	char *saveptr;
	int line;

	double arrivalTime;
	int LBN;
	int requestSize;
	int D;
	int cylinder;
	int PSN;
	int sectorOffset;
	int surface;
	int xlimit = 0;
	struct requestData *tempData;
	node *curData;

	// Write to out file
	fwrite = fopen(outputFile, "w");

	if (fwrite == NULL) {

		printf("\nError in output.\n");
		fclose(fwrite);
		exit(0);
	}


	// Read in each line of input file
	fread = fopen(inputFile, "r");
	if (fread != NULL) {
		while( fgets(buffer2, MAXLINESIZE, fread) != NULL ) {
			strcpy(temp,buffer2);
			q = temp;
			line = 0;
			while ((q=strtok_r(q, " \r\n\0", &saveptr)) != NULL) {
				if (line == 0) arrivalTime = atof(q);
				else if (line == 1) LBN = atoi(q);
				else requestSize = atoi(q);
				q = NULL;
				line++;
			}

			// Calculate necessary parameters
			PSN = LBN*8;
			cylinder = PSN/1600;
			D = (LBN*8)/1600 - curCylinder;
			if (D < 0) D=D*(-1);
			surface = ((LBN*8)%1600)/200;
			sectorOffset = ((PSN%1600)%200) + requestSize;

			// Check if current time is greater than the arrival time
			if (curTime > arrivalTime) {
				insert(list, createStruct(arrivalTime, LBN, requestSize, PSN, cylinder, surface, sectorOffset, D));
			}

			// If the new request hasn't arrived yet
			else {
				// Temporary store the current line
				tempData = createStruct(arrivalTime, LBN, requestSize, PSN, cylinder, surface, sectorOffset, D);

				// If the buffer isn't empty
				if (list->count > 0) {
					curData = find(list); // Find best seek time
					serviceSSTF(curData->val, fwrite); // Service the request
					deleteNode(list, curData); // Delete the request from the buffer
					insert(list, tempData); // Put the current line into buffer
				}

				// If the buffer is empty
				else {
					serviceSSTF(tempData, fwrite); // Just service the request
					free(tempData);
				}
			}

			xlimit++;
			if (xlimit == limit) break; // If limit is set
		}

		// Once all the lines from the input file have been read, loop through the buffer until it's empty
		while (list->count > 0) {
			curData = find(list); // Find best seek time
			serviceSSTF(curData->val, fwrite); // Service request
			deleteNode(list, curData); // Delete from buffer
		}

	} 
	else {

		printf("\nError in input.\n");
		fclose(fwrite);
		fclose(fread);
		exit(0);
	}

	fclose(fread);
	fclose(fwrite);
	deleteList(list);
}

//////////
// Main //
//////////

int main(int argc, char *argv[]) {
	char inputFile[FILESIZE], outputFile[FILESIZE], algorithm[ALGORITHMSIZE];
	int limit;
	strcpy(inputFile, argv[1]);
	strcpy(outputFile, argv[2]);
	strcpy(algorithm, argv[3]);
	if (argc == 5) limit = atoi(argv[4]);
	else limit = -1;
	
	if ( strcmp(algorithm, "fcfs") == 0 ) FCFS(inputFile, outputFile, limit);
	else if ( strcmp(algorithm, "sstf") == 0 ) SSTF(inputFile, outputFile, limit);

	return 0;
}
