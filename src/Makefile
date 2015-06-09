OBJS = Buffer.o Log.o Socket.o Script.o Config.o Process.o EventLoop.o Connection.o \
	Session.o Server.o Master.o Router.o Worker.o Backend.o fooking.o \
	Poll/EPoll.o Poll/Kqueue.o Poll/Select.o ../deps/lua/src/liblua.a
FLAG = -O -g -Wall -Wno-deprecated -I../src/ -I../deps/lua/src -L../deps/lua/src
LIBS =
BIN = fooking
CC = g++

all:$(OBJS)
	$(CC) -o $(BIN) $(FLAG) $(OBJS) $(LIBS)

%.o : %.cpp
	$(CC) -c $(FLAG) $< -o $@

.PHONY : clean

clean:
	rm -rf *.o
	rm -rf Poll/*.o
	rm -rf Protocol/*.o
	rm -f $(BIN)
