CXX = g++
CXXFLAGS = -O3 -fPIC -std=c++20
INCLUDES = -I/home/gamarino/Documentos/proyectos/protoPython/include -I/home/gamarino/Documentos/proyectos/protoCore/headers
LDFLAGS = -L/home/gamarino/Documentos/proyectos/protoPython/build/src/library -L/home/gamarino/Documentos/proyectos/protoPython/build/protoCore
LIBS = -lprotoPython -lprotoCore

SRCS = test_fstring.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = module.so

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -shared $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
