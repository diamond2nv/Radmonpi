
# gcc -lmmal_core -lmmal_util -lmmal_vc_client -lvcos -lbcm_host -I/opt/vc/include/ -L/opt/vc/lib/ -o raspiraw raspiraw.c RaspiCLI.c

CC = gcc
CFLAGS = -I/opt/vc/include/ -Wall
LIBS = -L/opt/vc/lib/ -lmmal_core -lmmal_util -lmmal_vc_client -lvcos -lbcm_host # -L/usr/lib -lm -ldl -lstdc++ 
DEPS = adv7282m_modes.h imx219_modes.h ov5647_modes.h RaspiCLI.h raw_header.h
OBJDIR = obj
SRCDIR = src
BINDIR = .
OBJ = radmonpi.o mask_generator.o RaspiCLI.o 
MD = mkdir

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(addprefix $(SRCDIR)/, $(DEPS))
	@$(MD) -p $(OBJDIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@$(MD) -p $(OBJDIR)
	g++ -c -o $@ $<

.PHONY: all
all: radmonpi mask_generator simulate
	@echo "Successfully build"

radmonpi: $(OBJDIR)/radmonpi.o $(OBJDIR)/RaspiCLI.o
	@$(MD) -p $(BINDIR)
	$(CC) -o $(BINDIR)/$@ $^ $(CFLAGS) $(LIBS)

mask_generator: $(OBJDIR)/mask_generator.o $(OBJDIR)/RaspiCLI.o
	@$(MD) -p $(BINDIR)
	$(CC) -o $(BINDIR)/$@ $^ $(CFLAGS) $(LIBS)

simulate: $(OBJDIR)/simulate.o
	@$(MD) -p $(BINDIR)
	g++ -o $(BINDIR)/$@ $^

.PHONY: clean
clean:
	@rm -Rf $(OBJDIR) $(BINDIR)/radmonpi $(BINDIR)/mask_generator $(BINDIR)/simulate
	@echo "All clean!"