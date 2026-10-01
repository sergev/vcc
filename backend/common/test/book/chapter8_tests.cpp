//
// Chapter 8 — Loops: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_8/valid + extra_credit).
// Each program defines int main(void); b6sim --status prints its return value, and we
// compare program output against the value computed by host cc.
//
// Four programs the book lists as valid shadow an enclosing name (in a nested
// block, for-init, or case block); this compiler rejects shadowing by design,
// so they are semantic-negative tests in semantic/chapter8_tests.cpp instead
// (for_shadow, for_nested_shadow, case_block, switch_decl).
//
#include "book_test.h"

// --- valid ------------------------------------------------------------------

// while ((a = 1)) break; — body runs once, leaving a == 1.
TEST_F(BookTest, Chapter8_BreakImmediate)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 10;
    while ((a = 1))
        break;
    return a;
})"));
}

// break out of a for loop when a hits 0.
TEST_F(BookTest, Chapter8_Break)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 10;
    int b = 20;
    for (b = -20; b < 0; b = b + 1) {
        a = a - 1;
        if (a <= 0)
            break;
    }
    return a == 0 && b == -11;
})"));
}

// continue with an empty post clause.
TEST_F(BookTest, Chapter8_ContinueEmptyPost)
{
    EXPECT_EQ("30\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    for (int i = 0; i < 10;) {
        i = i + 1;
        if (i % 2)
            continue;
        sum = sum + i;
    }
    return sum;
})"));
}

// continue skips odd iterations of a for loop.
TEST_F(BookTest, Chapter8_Continue)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    int counter;
    for (int i = 0; i <= 10; i = i + 1) {
        counter = i;
        if (i % 2 == 0)
            continue;
        sum = sum + 1;
    }
    return sum == 5 && counter == 10;
})"));
}

// do break; while(...) — body runs once, condition never re-evaluated.
TEST_F(BookTest, Chapter8_DoWhileBreakImmediate)
{
    EXPECT_EQ("10\n", CompileAndRunBook(R"(int main(void) {
    int a = 10;
    do
        break;
    while ((a = 1));
    return a;
})"));
}

// basic do-while loop.
TEST_F(BookTest, Chapter8_DoWhile)
{
    EXPECT_EQ("16\n", CompileAndRunBook(R"(int main(void) {
    int a = 1;
    do {
        a = a * 2;
    } while(a < 11);
    return a;
})"));
}

// empty (null) statements.
TEST_F(BookTest, Chapter8_EmptyExpression)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    return 0;;;
})"));
}

// do ; while(...) — empty loop body.  The original textbook test counts i down
// from 2147483642 by 5 to 252 (~430M iterations, ~60s on Dubna — far over the
// 10s ctest timeout).  Start i at 302 instead: still ≡ 2 (mod 5) and > 256, so
// the loop still exits at 252, but in only 10 iterations of the empty body.
TEST_F(BookTest, Chapter8_EmptyLoopBody)
{
    EXPECT_EQ("252\n", CompileAndRunBook(R"(int main(void) {
    int i = 302;
    do ; while ((i = i - 5) >= 256);
    return i;
})"));
}

// for with no condition clause — exits via an inner return.
TEST_F(BookTest, Chapter8_ForAbsentCondition)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    for (int i = 400; ; i = i - 100)
        if (i == 100)
            return 0;
})"));
}

// for with no post clause.
TEST_F(BookTest, Chapter8_ForAbsentPost)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int a = -2147483647;
    for (; a % 5 != 0;) {
        a = a + 1;
    }
    return a % 5 || a > 0;
})"));
}

// for with a declaration in the init clause.
TEST_F(BookTest, Chapter8_ForDecl)
{
    EXPECT_EQ("101\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    for (int i = -100; i <= 0; i = i + 1)
        a = a + 1;
    return a;
})"));
}

// for with an expression (non-declaration) init clause.
TEST_F(BookTest, Chapter8_For)
{
    EXPECT_EQ("16\n", CompileAndRunBook(R"(int main(void) {
    int a = 12345;
    int i;
    for (i = 5; i >= 0; i = i - 1)
        a = a / 3;
    return a;
})"));
}

// two sequential while loops.
TEST_F(BookTest, Chapter8_MultiBreak)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int i = 0;
    while (1) {
        i = i + 1;
        if (i > 10)
            break;
    }
    int j = 10;
    while (1) {
        j = j - 1;
        if (j < 0)
            break;
    }
    int result = j == -1 && i == 11;
    return result;
})"));
}

// multiple continues in the same do-while loop.
TEST_F(BookTest, Chapter8_MultiContinueSameLoop)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    int y = 0;
    int z = 0;
    do {
        z = z + 1;
        if (x <= 0)
            continue;
        x = x - 1;
        if (y >= 10)
            continue;
        y = y + 1;
    } while (z != 50);
    return z == 50 && x == 0 && y == 10;
})"));
}

// break in the inner of two nested for loops.
TEST_F(BookTest, Chapter8_NestedBreak)
{
    EXPECT_EQ("250\n", CompileAndRunBook(R"(int main(void) {
    int ans = 0;
    for (int i = 0; i < 10; i = i + 1)
        for (int j = 0; j < 10; j = j + 1)
            if ((i / 2)*2 == i)
                break;
            else
                ans = ans + i;
    return ans;
})"));
}

// continue in the inner of two nested while loops.
TEST_F(BookTest, Chapter8_NestedContinue)
{
    EXPECT_EQ("24\n", CompileAndRunBook(R"(int main(void) {
    int x = 5;
    int acc = 0;
    while (x >= 0) {
        int i = x;
        while (i <= 10) {
            i = i + 1;
            if (i % 2)
                continue;
            acc = acc + 1;
        }
        x = x - 1;
    }
    return acc;
})"));
}

// nested while loops accumulating a count.
TEST_F(BookTest, Chapter8_NestedLoop)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int acc = 0;
    int x = 100;
    while (x) {
        int y = 10;
        x = x - y;
        while (y) {
            acc = acc + 1;
            y = y - 1;
        }
    }
    return acc == 100 && x == 0;
})"));
}

// for (;;) with a break.
TEST_F(BookTest, Chapter8_NullForHeader)
{
    EXPECT_EQ("4\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    for (; ; ) {
        a = a + 1;
        if (a > 3)
            break;
    }
    return a;
})"));
}

// basic while loop.
TEST_F(BookTest, Chapter8_While)
{
    EXPECT_EQ("6\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    while (a < 5)
        a = a + 2;
    return a;
})"));
}

// --- extra_credit -----------------------------------------------------------

// compound assignment as a do-while controlling expression.
TEST_F(BookTest, Chapter8_CompoundAssignmentControllingExpression)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int i = 100;
    int sum = 0;
    do sum += 2;
    while (i -= 1);
    return (i == 0 && sum == 200);
})"));
}

// compound assignment in for-init and for-post.
TEST_F(BookTest, Chapter8_CompoundAssignmentForLoop)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int i = 1;
    for (i *= -1; i >= -100; i -=3)
        ;
    return (i == -103);
})"));
}

// Duff's device — switch fallthrough interleaved with a do-while loop.
TEST_F(BookTest, Chapter8_DuffsDevice)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int count = 37;
    int iterations = (count + 4) / 5;
    switch (count % 5) {
        case 0:
            do {
                count = count - 1;
                case 4:
                    count = count - 1;
                case 3:
                    count = count - 1;
                case 2:
                    count = count - 1;
                case 1:
                    count = count - 1;
            } while ((iterations = iterations - 1) > 0);
    }
    return (count == 0 && iterations == 0);
})"));
}

// goto jumps past a do-while controlling condition.
TEST_F(BookTest, Chapter8_GotoBypassCondition)
{
    EXPECT_EQ("10\n", CompileAndRunBook(R"(int main(void) {
    int i = 1;
    do {
    while_start:
        i = i + 1;
        if (i < 10)
            goto while_start;
    } while (0);
    return i;
})"));
}

// goto jumps into the middle of a for loop, skipping the init clause.
TEST_F(BookTest, Chapter8_GotoBypassInitExp)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int i = 0;
    goto target;
    for (i = 5; i < 10; i = i + 1)
    target:
        if (i == 0)
            return 1;
    return 0;
})"));
}

// goto jumps backward within a for loop, skipping the post clause.
TEST_F(BookTest, Chapter8_GotoBypassPostExp)
{
    EXPECT_EQ("11\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    for (int i = 0;; i = 0) {
    lbl:
        sum = sum + 1;
        i = i + 1;
        if (i > 10)
            break;
        goto lbl;
    }
    return sum;
})"));
}

// a loop body may be a labeled statement.
TEST_F(BookTest, Chapter8_LabelLoopBody)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int result = 0;
    goto label;
    while (0)
    label: { result = 1; }
    return result;
})"));
}

// do/while/for may each be labeled; goto threads between them.
TEST_F(BookTest, Chapter8_LabelLoopsBreaksAndContinues)
{
    EXPECT_EQ("12\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    goto do_label;
    return 0;

do_label:
    do {
        sum = 1;
        goto while_label;
    } while (1);

while_label:
    while (1) {
        sum = sum + 1;
        goto break_label;
        return 0;
    break_label:
        break;
    };
    goto for_label;
    return 0;

for_label:
    for (int i = 0; i < 10; i = i + 1) {
        sum = sum + 1;
        goto continue_label;
        return 0;
    continue_label:
        continue;
        return 0;
    }
    return sum;
})"));
}

// postfix-- and prefix-- as while controlling expressions.
TEST_F(BookTest, Chapter8_LoopHeaderPostfixAndPrefix)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int i = 100;
    int count = 0;
    while (i--) count++;
    if (count != 100)
        return 0;
    i = 100;
    count = 0;
    while (--i) count++;
    if (count != 99)
        return 0;
    return 1;
})"));
}

// break inside a switch inside a for breaks the loop only via 'return'.
TEST_F(BookTest, Chapter8_LoopInSwitch)
{
    EXPECT_EQ("123\n", CompileAndRunBook(R"(int main(void) {
    int cond = 10;
    switch (cond) {
        case 1:
            return 0;
        case 10:
            for (int i = 0; i < 5; i = i + 1) {
                cond = cond - 1;
                if (cond == 8)
                    break;
            }
            return 123;
        default:
            return 2;
    }
    return 3;
})"));
}

// postfix ++ as a for post-expression.
TEST_F(BookTest, Chapter8_PostExpIncr)
{
    EXPECT_EQ("21\n", CompileAndRunBook(R"(int main(void) {
    int product = 1;
    for (int i = 0; i < 10; i++) {
        product = product + 2;
    }
    return product;
})"));
}

// an assignment as the switch controlling expression.
TEST_F(BookTest, Chapter8_SwitchAssignInCondition)
{
    EXPECT_EQ("2\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    switch (a = 1) {
        case 0:
            return 10;
        case 1:
            a = a * 2;
            break;
        default:
            a = 99;
    }
    return a;
})"));
}

// basic switch with break.
TEST_F(BookTest, Chapter8_SwitchBreak)
{
    EXPECT_EQ("10\n", CompileAndRunBook(R"(int main(void) {
    int a = 5;
    switch (a) {
        case 5:
            a = 10;
            break;
        case 6:
            a = 0;
            break;
    }
    return a;
})"));
}

// fall through from a non-last default into a following case.
TEST_F(BookTest, Chapter8_SwitchDefaultFallthrough)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int a = 5;
    switch(0) {
        default:
            a = 0;
        case 1:
            return a;
    }
    return a + 1;
})"));
}

// default before case in the body.
TEST_F(BookTest, Chapter8_SwitchDefaultNotLast)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    int a;
    int b = a = 7;
    switch (a + b) {
        default: return 0;
        case 2: return 1;
    }
})"));
}

// a switch whose body is a single default (unbraced).
TEST_F(BookTest, Chapter8_SwitchDefaultOnly)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 1;
    switch(a) default: return 1;
    return 0;
})"));
}

// switch falling through to a default.
TEST_F(BookTest, Chapter8_SwitchDefault)
{
    EXPECT_EQ("22\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    switch(a) {
        case 1:
            return 1;
        case 2:
            return 9;
        case 4:
            a = 11;
            break;
        default:
            a = 22;
    }
    return a;
})"));
}

// empty switch bodies still evaluate the controlling expression.
TEST_F(BookTest, Chapter8_SwitchEmpty)
{
    EXPECT_EQ("12\n", CompileAndRunBook(R"(int main(void) {
    int x = 10;
    switch(x = x + 1) {

    }
    switch(x = x + 1)
    ;
    return x;
})"));
}

// fallthrough between cases without breaks.
TEST_F(BookTest, Chapter8_SwitchFallthrough)
{
    EXPECT_EQ("6\n", CompileAndRunBook(R"(int main(void) {
    int a = 4;
    int b = 9;
    int c = 0;
    switch (a ? b : 7) {
        case 0:
            return 5;
        case 7:
            c = 1;
        case 9:
            c = 2;
        case 1:
            c = c + 4;
    }
    return c;
})"));
}

// goto into the middle of a case label.
TEST_F(BookTest, Chapter8_SwitchGotoMidCase)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    goto mid_case;
    switch (4) {
        case 4:
            a = 5;
        mid_case:
            a = a + 1;
            return a;
    }
    return 100;
})"));
}

// break inside a switch inside a loop breaks the switch, not the loop.
TEST_F(BookTest, Chapter8_SwitchInLoop)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int acc = 0;
    int ctr = 0;
    for (int i = 0; i < 10; i = i + 1)  {
        switch(i) {
            case 0:
                acc = 2;
                break;
            case 1:
                acc = acc * 3;
                break;
            case 2:
                acc = acc * 4;
                break;
            default:
                acc = acc + 1;
        }
        ctr = ctr + 1;
    }
    return ctr == 10 && acc == 31;
})"));
}

// case labels reachable inside nested if/else/for bodies.
TEST_F(BookTest, Chapter8_SwitchNestedCases)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int switch1 = 0;
    int switch2 = 0;
    int switch3 = 0;
    switch(3) {
        case 0: return 0;
        case 1: if (0) {
            case 3: switch1 = 1; break;
        }
        default: return 0;
    }
    switch(4) {
        case 0: return 0;
        if (1) {
            return 0;
        } else {
            case 4: switch2 = 1; break;
        }
        default: return 0;
    }
    switch (5) {
        for (int i = 0; i < 10; i = i + 1) {
            switch1 = 0;
            case 5: switch3 = 1; break;
            default: return 0;
        }
    }
    return (switch1 && switch2 && switch3);
})"));
}

// an outer switch does not jump to a nested switch's cases.
TEST_F(BookTest, Chapter8_SwitchNestedNotTaken)
{
    EXPECT_EQ("2\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    switch(a) {
        case 1:
            switch(a) {
                case 0: return 0;
                default: return 0;
            }
        default: a = 2;
    }
    return a;
})"));
}

// both outer and inner switch cases execute.
TEST_F(BookTest, Chapter8_SwitchNestedSwitch)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void){
    switch(3) {
        case 0:
            return 0;
        case 3: {
            switch(4) {
                case 3: return 0;
                case 4: return 1;
                default: return 0;
            }
        }
        case 4: return 0;
        default: return 0;
    }
})"));
}

// a switch body with no case labels executes nothing.
TEST_F(BookTest, Chapter8_SwitchNoCase)
{
    EXPECT_EQ("4\n", CompileAndRunBook(R"(int main(void) {
    int a = 4;
    switch(a)
        return 0;
    return a;
})"));
}

// a switch with cases but no match and no default executes nothing.
TEST_F(BookTest, Chapter8_SwitchNotTaken)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 1;
    switch(a) {
        case 0: return 0;
        case 2: return 0;
        case 3: return 0;
    }
    return 1;
})"));
}

// a switch whose body is a single (unbraced) case.
TEST_F(BookTest, Chapter8_SwitchSingleCase)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 1;
    switch(a) case 1: return 1;
    return 0;
})"));
}

// continue in a switch inside a loop (variant 2).
TEST_F(BookTest, Chapter8_SwitchWithContinue2)
{
    EXPECT_EQ("5\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
        switch(i % 2) {
            case 0: continue;
            default: sum = sum + 1;
        }
    }
    return sum;
})"));
}

// continue in a loop nested inside a switch case.
TEST_F(BookTest, Chapter8_SwitchWithContinue)
{
    EXPECT_EQ("5\n", CompileAndRunBook(R"(int main(void) {
    switch(4) {
        case 0:
            return 0;
        case 4: {
            int acc = 0;
            for (int i = 0; i < 10; i = i + 1) {
                if (i % 2)
                    continue;
                acc = acc + 1;
            }
            return acc;
        }
    }
    return 0;
})"));
}

// a very simple switch.
TEST_F(BookTest, Chapter8_Switch)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int main(void) {
    switch(3) {
        case 0: return 0;
        case 1: return 1;
        case 3: return 3;
        case 5: return 5;
    }
})"));
}
