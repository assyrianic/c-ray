
# `make lib` -- Build c-ray as a .so + driver instead of a monolith

LIB=lib/libc-ray.so
BIN_lib=lib/c-ray
OBJDIR_lib=lib/obj_lib
OBJDIR_driver=lib/obj_driver
SRCS_lib=$(shell find src/lib src/common generated/ -name '*.c')
OBJS_lib=$(patsubst %.c, $(OBJDIR_lib)/%.o, $(SRCS_lib))
SRCS_driver=$(shell find src/driver src/common -name '*.c')
OBJS_driver=$(patsubst %.c, $(OBJDIR_driver)/%.o, $(SRCS_driver))

lib: $(BIN_lib)

pylib: wrappers/cray_wrap.so

$(OBJDIR_driver)/%.o: %.c $(OBJDIR_driver)
	@mkdir -p '$(@D)'
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR_lib)/%.o: %.c $(OBJDIR_lib)
	@mkdir -p '$(@D)'
	@echo "CC -fPIC $<"
	@$(CC) -DCR_BUILDING_LIB $(CFLAGS) -fvisibility=hidden -c -fPIC $< -o $@
$(OBJDIR_lib): dummy
	mkdir -p $@
$(OBJDIR_driver):
	mkdir -p $@
$(LIB): $(OBJS_lib)
	@echo "LD -fPIC $@"
	@$(CC) $(CFLAGS) $(OBJS_lib) -shared -o $@
$(BIN_lib): $(LIB) $(OBJS_driver) $(OBJDIR_driver)
	@echo "LD $@"
	@$(CC) $(CFLAGS) $(OBJS_driver) $(LIB) -o $@ $(LDFLAGS)
wrappers/cray_wrap.so: $(LIB) wrappers/cray_wrap.c
	@echo "Building Python module"
	@$(CC) -shared $(CFLAGS) -fPIC `pkg-config --cflags python3` wrappers/cray_wrap.c $(LIB) -o $@
