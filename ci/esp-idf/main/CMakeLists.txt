# No REQUIRES: the main component then depends on every component, including
# the SDK one (whose name is the checkout directory's name).
idf_component_register(SRCS "main.c" INCLUDE_DIRS "../../arduino/vban_smoke")
