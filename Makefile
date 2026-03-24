CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Iinclude
LDFLAGS = -lcurl

SRCDIR = src
OBJDIR = obj
BINDIR = bin

TARGET = $(BINDIR)/enclosed
SRCS = $(wildcard $(SRCDIR)/*.cpp)
OBJS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
HDRS = $(wildcard include/*.hpp)

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HDRS) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all clean
