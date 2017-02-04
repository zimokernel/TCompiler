/*************************************************************************
	> File Name: utiltest.c
	> Author:TTc 
	> Mail:liutianshxkernel@gmail.com 
	> Created Time: Fri Oct 28 10:07:17 2016
 ************************************************************************/

#include <stdio.h>
#include <string.h>
#include "tcc.h"

void
assert_equal(char *s, char *t) {
    if (strcmp(s, t)) {
        error("Expected %s but got %s", s, t);
    }
}

void
test_string() {
    String *s = make_string();
    string_append(s, 'a');
    assert_equal("a", get_cstring(s));
    string_append(s, 'b');
    assert_equal("ab", get_cstring(s));
    
    string_appendf(s, ".");
    assert_equal("ab.", get_cstring(s));
    string_appendf(s, "%s","0123456789");
    assert_equal("ab.0123456789", get_cstring(s));
}

int
main(int argc, char **argv) {
    test_string();
    printf("All passed\n");
    return 0;
}
