dest_dir = build
src_dir = src
obj_dir = $(dest_dir)/objects
bin_dir = $(dest_dir)/bin
	
CC = gcc
CFLAGS = -g -Wall
CFILES = main.c util.c parser.c argtable3.c c_cpp_file_io.c c_cpp.c general.c
ofiles = $(CFILES:%.c=$(obj_dir)/%.o)

program = $(bin_dir)/judger
$(program): $(ofiles)

$(bin_dir)/%:
	@echo ">>> Linking" $@ "<<<"
	$(CC) $^ -o $@
	ln -sf $@ $(notdir $@)

$(obj_dir)/%.o: $(src_dir)/%.c
	@echo ">>> Compiling" $< "<<<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(obj_dir)/%.o: $(src_dir)/argtable/%.c
	@echo ">>> Compiling" $< "<<<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(obj_dir)/%.o: $(src_dir)/rules/%.c
	@echo ">>> Compiling" $< "<<<"
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f `find $(dest_dir) -type f -print | egrep -v '(CVS|cvsignore)'`
	rm -f judger