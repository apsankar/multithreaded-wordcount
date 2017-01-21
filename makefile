all: wordCount

wordCount: wordCount.c
	gcc -pthread wordCount.c -o wordCount

clean:
	rm wordCount wordCount.txt