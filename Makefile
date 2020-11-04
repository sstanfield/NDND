CXX = g++
CXXFLAGS = -std=c++14 -Wall `pkg-config --cflags libndn-cxx` -g
LIBS = `pkg-config --libs libndn-cxx`
DESTDIR ?= /usr/local
SRC_DIR = .
SOURCES = nd-client.cpp ahclient.cpp multicast.cpp
SOURCE_OBJS = nd-client.o ahclient.o multicast.o #nd-app.o
PROGRAMS = nd-client

all: $(PROGRAMS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(LIBS)

nd-client: $(SOURCE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ nd-client.o ahclient.o multicast.o $(LIBS)

clean:
	rm -f $(PROGRAMS) *.o

install: all
	cp $(PROGRAMS) $(DESTDIR)/bin/

uninstall:
	cd $(DESTDIR)/bin && rm -f $(PROGRAMS)

fmt:
	@for src in $(SOURCES) ; do \
		echo "Formatting $$src..." ; \
		clang-format -i "$(SRC_DIR)/$$src" ; \
	done
	@echo "Done"

style:
	@for src in $(SOURCES) ; do \
		echo "[STYLE CHECKING $$src]" ; \
		clang-tidy -checks='-*,readability-identifier-naming' \
		    -config="{CheckOptions: [ \
		    { key: readability-identifier-naming.NamespaceCase, value: lower_case },\
		    { key: readability-identifier-naming.ClassCase, value: CamelCase  },\
		    { key: readability-identifier-naming.StructCase, value: CamelCase  },\
		    { key: readability-identifier-naming.FunctionCase, value: camelBack },\
		    { key: readability-identifier-naming.VariableCase, value: lower_case },\
		    { key: readability-identifier-naming.GlobalConstantCase, value: UPPER_CASE }\
		    ]}" -header-filter=".*" "$(SRC_DIR)/$$src" -- $(CXXFLAGS) $(LIBS) -c $(SRC_DIR)/$$src ; \
	done
	@echo "Done"

check-fmt:
	@for src in $(SOURCES) ; do \
		var=`clang-format "$(SRC_DIR)/$$src" | diff "$(SRC_DIR)/$$src" - | wc -l` ; \
		if [ $$var -ne 0 ] ; then \
			echo "$$src does not respect the coding style (diff: $$var lines)" ; \
			exit 1 ; \
		fi ; \
	done
	@echo "Style check passed"

tidy-ALL:
	@for src in $(SOURCES) ; do \
		echo "" ; \
		echo "[LINTING ALL $$src]" ; \
		clang-tidy -checks="*" "$(SRC_DIR)/$$src" -- $(CXXFLAGS) $(LIBS) -c $(SRC_DIR)/$$src ; \
	done
	@echo "Done"

tidy:
	@for src in $(SOURCES) ; do \
		echo "" ; \
		echo "Running tidy on $$src..." ; \
		clang-tidy -checks="-*,modernize-*,readability-*,clang-analyzer-*,cppcoreguidelines-*,performance-*" \
		    -config="{CheckOptions: [ \
		    { key: readability-identifier-naming.NamespaceCase, value: lower_case },\
		    { key: readability-identifier-naming.ClassCase, value: CamelCase  },\
		    { key: readability-identifier-naming.StructCase, value: CamelCase  },\
		    { key: readability-identifier-naming.FunctionCase, value: camelBack },\
		    { key: readability-identifier-naming.VariableCase, value: lower_case },\
		    { key: readability-identifier-naming.GlobalConstantCase, value: UPPER_CASE }\
			]}" -header-filter=".*" \
			"$(SRC_DIR)/$$src" -- $(CXXFLAGS) $(LIBS) -c $(SRC_DIR)/$$src ; \
	done
	@echo "Done"
