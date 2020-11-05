CXX = g++
CXXFLAGS = -std=c++14 -Wall -Werror `pkg-config --cflags libndn-cxx`
LIBS = `pkg-config --libs libndn-cxx`
DESTDIR ?= /usr/local
SRC_DIR = src
SOURCES = nd-client.cpp ahclient.cpp multicast.cpp
OBJS = $(SOURCES:.cpp=.o)
EXE  = ah-ndn
DEPS = $(OBJS:%.o=%.d)
BUILD_DIR = build

ifeq ($(RELEASE),1)
BLDDIR = $(BUILD_DIR)/release
EXTRAFLAGS = -O3 -DNDEBUG
else
BLDDIR = $(BUILD_DIR)/debug
EXTRAFLAGS = -g -O0 -DDEBUG
endif

BLDEXE = $(BLDDIR)/$(EXE)
BLDOBJS = $(addprefix $(BLDDIR)/, $(OBJS))
BLDDEPS = $(addprefix $(BLDDIR)/, $(DEPS))

SOURCE_OBJS = nd-client.o ahclient.o multicast.o

.PHONY: all depend clean debug prep release remake install uninstall fmt style check-fmt tidy-ALL tidy

# Default build
all: prep $(BLDEXE)

# Include all .d files
-include $(BLDDEPS)

$(BLDEXE): $(BLDOBJS)
	$(CXX) $(CXXFLAGS) $(EXTRAFLAGS) -o $(BLDEXE) $^ $(LIBS)

$(BLDDIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $(CXXFLAGS) $(EXTRAFLAGS) -MMD -o $@ $< $(LIBS)

#
# Other rules
#
prep:
	@mkdir -p $(BLDDIR)

remake: clean all

clean:
	rm -rf $(BUILD_DIR)/

install: all
	cp $(BLDEXE) $(DESTDIR)/bin/

uninstall:
	cd $(DESTDIR)/bin && rm -f $(EXE)

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



