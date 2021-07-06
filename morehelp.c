#include <stdio.h>

void print_help(void)
{
  puts("Most commands optionally preceded by integer argument k.  Defaults in brackets.");
  puts("Star (*) indicates argument becomes new default.");
  puts("-------------------------------------------------------------------------------");
  puts("<space>                 Display next k lines of text [current screen size]");
  puts("z                       Display next k lines of text [current screen size]*");
  puts("<return>                Display next k lines of text [1]*");
  puts("d or ctrl-D             Scroll k lines [current scroll size, initially 11]*");
  puts("q or Q or <interrupt>   Exit from more");
  puts("s                       Skip forward k lines of text [1]");
  puts("f                       Skip forward k screenfuls of text [1]");
  puts("b or ctrl-B             Skip backwards k screenfuls of text [1]");
  puts("'                       Go to place where previous search started");
  puts("=                       Display current line number");
  puts("/<regular expression>   Search for kth occurrence of regular expression [1]");
  puts("n                       Search for kth occurrence of last r.e [1]");
  puts("!<cmd> or :!<cmd>       Execute <cmd> in a subshell");
  puts("v                       Start up an editor at current line");
  puts("ctrl-L                  Redraw screen");
  puts(":n                      Go to kth next file [1]");
  puts(":p                      Go to kth previous file [1]");
  puts(":f                      Display current file name and line number");
  puts(".                       Repeat previous command");
  puts("-------------------------------------------------------------------------------");
}
