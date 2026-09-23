CXX = g++
CXXFLAGS = -std=c++11 -Wall -g -O2 -I/usr/local/include -I/usr/include/mysql -I/usr/include/jsoncpp
LDFLAGS = -L/usr/local/lib -lmuduo_net -lmuduo_base -lmuduo_http -lmariadb -lhiredis -ljsoncpp -lpthread -lrt

TARGET = chat_server
SRCDIR = src
OBJDIR = obj
BINDIR = bin

SOURCES = $(SRCDIR)/main.cc $(SRCDIR)/ChatServer.cc $(SRCDIR)/HttpServer.cc \
          $(SRCDIR)/Database.cc $(SRCDIR)/RedisClient.cc
OBJECTS = $(SOURCES:$(SRCDIR)/%.cc=$(OBJDIR)/%.o)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(BINDIR)/$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CXX) -o $@ $^ $(LDFLAGS)

all: $(BINDIR)/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(BINDIR)

run: $(BINDIR)/$(TARGET)
	./$(BINDIR)/$(TARGET)

.PHONY: all clean run