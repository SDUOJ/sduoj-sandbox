dest_dir = build
src_dir = src
obj_dir = $(dest_dir)/objects
bin_dir = $(dest_dir)/bin
	
CC = gcc
LD = gcc
CFLAGS = -g -Wall -Werror -O3 -std=c99 -pie -fPIC
LDFLAGS = 
LIBS = -lpthread -lseccomp
CFILES = main.c util.c parser.c argtable3.c c_cpp.c general.c container.c logger.c
ofiles = $(CFILES:%.c=$(obj_dir)/%.o)

program = $(bin_dir)/sandbox
$(program): $(ofiles)

$(bin_dir)/%:
	@echo ">>> Linking" $@ "<<<"
	@if [ ! -d $(bin_dir) ]; then mkdir -p $(bin_dir); fi;
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

$(obj_dir)/%.o: $(src_dir)/%.c
	@echo ">>> Compiling" $< "<<<"
	@if [ ! -d $(obj_dir) ]; then mkdir -p $(obj_dir); fi;
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

$(obj_dir)/%.o: $(src_dir)/*/%.c
	@echo ">>> Compiling" $< "<<<"
	@if [ ! -d $(obj_dir) ]; then mkdir -p $(obj_dir); fi;
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

.PHONY: install clean
install:
	ln -s $(shell pwd)/$(program) /usr/bin/sandbox
clean:
	# rm -f `find $(dest_dir) -type f -print | egrep -v '(CVS|cvsignore)'`
	rm -rf $(dest_dir)
	rm -f /usr/bin/sandbox
	rm -f sandbox.log