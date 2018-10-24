#include "csiebox_server.h"

int main(int argc, char** argv) {
  csiebox_server* box = 0;
  csiebox_server_init(&box, argc, argv);
  if (!box) {
    return 1;
  }
  csiebox_server_run(box);
  csiebox_server_destroy(&box);
  return 0;
}
