PROJECT = nweb
SOURCE = nweb.c
#HEADERS = 
#CC = gcc 
CFLAGS = -Wall -g -pthread
LDFLAGS = -pthread
OBJECTS = ${SOURCE:.c=.o}

$(PROJECT): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(PROJECT) $(LDFLAGS)

$(OBJECTS): $(HEADERS)

clean:
	@rm -f $(PROJECT) $(OBJECTS) *.core


