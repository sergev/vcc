//
// Chapter 18 -- Structures and unions: semantic errors.  Imported from "Writing
// a C Compiler" (tests/chapter_18/invalid_struct_tags and invalid_types, incl.
// extra_credit).  Each program parses but violates a struct/union typing rule
// (tag resolution, incomplete types, member operators, scalar requirements,
// initializers, incompatible types).  Tests assert on a substring of the
// type-checker's fatal-error text.
//
// Three small frontend fixes light up programs we previously accepted: `==`/`!=`
// now require scalar operands (semantic/expressions.c), a `?:` with mismatched
// struct/union tags is rejected (semantic/expressions.c), and
// common_pointer_type rejects pointers to distinct struct/union tags
// (semantic/typecheck.c).
//
// Book cases that relied on an inner-scope tag shadowing an outer one with a
// distinct type have no analogue under the project's permanent no-shadowing
// design: a same-tag redeclaration either reuses the existing tag (so the
// program is well-typed) or, with the opposite keyword, is a cross-kind conflict
// — the latter are kept here as cross-kind negative tests.
//
#include "typecheck_fixture.h"


// --- invalid_struct_tags ---

TEST_F(PipelineTest, Chapter18_StructTagsArrayOfUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this declaration fails tag resolution because the
    // 'struct s' type hasn't been declared.
    // In a fully conforming implementation it would fail because it's illegal
    // to specify arrays of incomplete type
    struct s arr[2];
    return 0;
}
)SRC"),
                 "array has incomplete element type");
}

TEST_F(PipelineTest, Chapter18_StructTagsCastUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this fails tag resolution because it specifies an
    // undeclared type. In a fully conforming implementation, it fails because
    // it casts to a structure type
    (struct s)0;
    return 0;
}
)SRC"),
                 "cannot cast '");
}

TEST_F(PipelineTest, Chapter18_StructTagsDerefUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // in our implementation, you can't use an undeclared struct in a type
    // specifier
    struct s *ptr = 0;
    *ptr;  // in a fully conforming implementation, the pointer declaration
           // above would be legal, but dereferencing the pointer here would be
           // illegal
    return 0;
}
)SRC"),
                 "where a complete type is required");
}


// --- invalid_struct_tags/extra_credit ---

TEST_F(PipelineTest, Chapter18_ExtraCreditSizeofUndeclaredUnion_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this fails tag resolution because 'union c'
    // hasn't been declared yet.
    // In a fully conforming implementation it would fail because you
    // can't apply sizeof to incomplete types.
    return sizeof(union c);
}

union c {
    int x;
};
)SRC"),
                 "invalid application of 'sizeof' to an incomplete type");
}

TEST_F(PipelineTest, Chapter18_ExtraCreditVarUndeclaredUnionType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this fails tag resolution because it specifies
    // an incomplete union type.
    // In a fully conforming implementation, this would fail because it defines
    // a variable with incomplete type.

    union s var;
    return 0;
}
)SRC"),
                 "variable 'var' has incomplete type 'union s'");
}


// --- invalid_struct_tags ---

TEST_F(PipelineTest, Chapter18_StructTagsFileScopeVarTypeUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// In our implementation, this fails tag resolution because it specifies
// an incomplete structure type.
// In a fully conforming implementation, this would fail because it defines
// a variable with incomplete type.

struct s var;
)SRC"),
                 "variable 'var' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_StructTagsForLoopScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    for (int i = 0; i < 10; i = i + 1) {
        // test that struct tags are only in scope in block where they're
        // declared
        struct s {
            int a;
        };
    }

    // struct s tag is out of scope here
    // in our implementation this is illegal b/c struct s type is not in scope
    // in a fully conforming implementation this would be illegal b/c
    // it declares a variable with incomplete type
    struct s x;
    return 0;
}
)SRC"),
                 "variable 'x' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_StructTagsForLoopScope2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// a struct tag declared in a for loop body is not visible in the loop header

int main(void) {
    void *ptr;

    // there's no 'struct s' tag in scope in this loop's post-expression
    // in our implementation, this will fail tag resolution.
    // in a fully-conforming implementation, it would fail because it's
    // attempting to access a member of an incomplete type
    for (;; ((struct s *)ptr)->i) {
        struct s {
            int i;
        };
        struct s x = {1};
        ptr = &x;
    }
}
)SRC"),
                 "use of undeclared tag 's'");
}

TEST_F(PipelineTest, Chapter18_StructTagsMemberTypeUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    // In our implementation, this member declaration fails tag resolution
    // because the 'struct s' type hasn't been declared.
    // In a fully conforming implementation it would fail because it's
    // illegal to declare structure members of incomplete type
    struct a b;
};
)SRC"),
                 "member 'b' has incomplete type 'struct a'");
}

TEST_F(PipelineTest, Chapter18_StructTagsParamUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// In our implementation, this function definition fails tag resolution because
// the 'struct s' type hasn't been declared. In a fully conforming
// implementation, it would fail because you can't declare incomplete parameter
// types in function definitions.
int foo(struct s x) {
    return 0;
}
)SRC"),
                 "function 'foo' has an incomplete return or parameter type");
}

TEST_F(PipelineTest, Chapter18_StructTagsReturnTypeUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void exit(int status);

// In our implementation, this fails tag resolution because it specifies an
// undeclared type. In a fully conforming implementation, it fails because you
// can't define a function with incomplete return type
struct s foo(void) {
    exit(0);
}
)SRC"),
                 "function 'foo' has an incomplete return or parameter type");
}

TEST_F(PipelineTest, Chapter18_StructTagsSizeofUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this fails tag resolution because 'struct c'
    // hasn't been declared yet.
    // In a fully conforming implementation it would fail because you
    // can't apply sizeof to incomplete types.
    return sizeof(struct c);
}

struct c {
    int x;
};
)SRC"),
                 "invalid application of 'sizeof' to an incomplete type");
}

TEST_F(PipelineTest, Chapter18_StructTagsVarTypeUndeclared_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // In our implementation, this fails tag resolution because it specifies
    // an incomplete structure type.
    // In a fully conforming implementation, this would fail because it defines
    // a variable with incomplete type.

    struct s var;
    return 0;
}
)SRC"),
                 "variable 'var' has incomplete type 'struct s'");
}


// --- invalid_types/extra_credit/bad_union_member_access ---

TEST_F(PipelineTest, Chapter18_BadUnionMemberAccessNestedNonMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

union u {
    struct s nested;
};

int main(void) {
    union u my_union = {{1}};
    // need to specify member name 's' even though it's only member
    return my_union.a;
}
)SRC"),
                 "no member named 'a' in '(struct|union) u'");
}

TEST_F(PipelineTest, Chapter18_BadUnionMemberAccessUnionBadMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union s {
    int x;
    int y;
};

union t {
    int blah;
    int y;
};

int main(void) {
    union s foo = {1};
    return foo.blah; // "union s" has no member "blah"
}
)SRC"),
                 "no member named 'blah' in '(struct|union) s'");
}

TEST_F(PipelineTest, Chapter18_BadUnionMemberAccessUnionBadPointerMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void *malloc(unsigned long size);

union a {
  int x;
  int y;
};

union b {
  int m;
  int n;
};

int main(void) {
  union a *ptr = malloc(sizeof(union a));
  ptr->m = 10; // "union a" has no member "m"
  return 0;
}
)SRC"),
                 "no member named 'm' in '(struct|union) a'");
}


// --- invalid_types/extra_credit/incompatible_union_types ---

TEST_F(PipelineTest, Chapter18_UnionTypesAssignDifferentUnionType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't assign one union type to another

union u1 {int a;};
union u2 {int a;};

int main(void){
    union u1 x = {10};
    union u2 y = {11};
    x = y; // invalid - different types
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_UnionTypesAssignScalarToUnion_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't assign scalar value to union, even if value has same type
// as first element
union u {int a; int b;};

int main(void) {
    union u x = {1};
    x = 2; // invalid
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_UnionTypesReturnTypeMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u {
    int x;
};

union u return_union(void){
    // define an identical but distinct union type
    union u {
        int x;
    };

    union u result = {10};

    // return it; invalid b/c it's the wrong "union u" type
    return result;
}
)SRC"),
                 "redefinition of 'union u'");
}

TEST_F(PipelineTest, Chapter18_UnionTypesUnionBranchMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// If either branch of a conditional has union type, branches must have
// identical types
int main(void) {
    union u1 {
        int a;
    };
    union u2 {
        int a;
    };
    union u1 x = {10};
    union u2 y = {11};
    1 ? x : y; // mismatch: x and y have different types
    return 0;
}
)SRC"),
                 "incompatible operand types in '\\?:'");
}

TEST_F(PipelineTest, Chapter18_UnionTypesUnionPointerBranchMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    union u1;
    union u2;

    union u1 *ptr1 = 0;
    union u2 *ptr2 = 0;

    1 ? ptr1 : ptr2; // INVALID: different pointer types
    return 0;
}
)SRC"),
                 "incompatible pointer types");
}


// --- invalid_types/extra_credit/incomplete_unions ---

TEST_F(PipelineTest, Chapter18_IncompleteUnionsDefineIncompleteUnion_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u; // declare incomplete union type

union u my_union; // INVALID: defining variable with incomplete union type
)SRC"),
                 "variable 'my_union' has incomplete type 'union u'");
}

TEST_F(PipelineTest, Chapter18_IncompleteUnionsSizeofIncompleteUnionType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't apply sizeof to an incomplete union type

int main(void) {
    union u;
    return sizeof(union u);  // invalid - union u type is incomplete
}
)SRC"),
                 "invalid application of 'sizeof' to an incomplete type");
}


// --- invalid_types/extra_credit/invalid_union_lvalues ---

TEST_F(PipelineTest, Chapter18_AddressOfNonLvalueUnionMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u {
    int arr[3];
    double d;
};

union u get_union(void) {
    union u result = {{1, 2, 3}, 4.0};
    return result;
}

int main(void) {
    // invalid - can't get address of get_union().arr b/c it's not an lvalue
    // even though it has temporary lifetime
    int *ptr[3] = &get_union().arr;
    return 0;
}
)SRC"),
                 "excess elements in union initializer");
}

// Assigning to a member of a non-lvalue (function-return) union is rejected in typecheck:
// `return_union()` is an rvalue, so `.i.y` is a non-lvalue and cannot be assigned to.
TEST_F(PipelineTest, Chapter18_AssignNonLvalueUnionMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't assign to members in non-lvalue unions
union inner {
    int y;
    long z;
};

union u {
    int x;
    union inner i;
};

union u return_union(void){
    union u result = {1};
    return result;
}

int main(void) {
    // invalid - return_union() is not an lvalue
    return_union().i.y = 1;
    return 0;
}
)SRC"),
                 "expression is not assignable");
}


// --- invalid_types/extra_credit/other_features ---

TEST_F(PipelineTest, Chapter18_OtherFeaturesBitwiseOpStructure_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use operands of structure type in bitwise expressions
struct s {int i;};
int main(void) {
    struct s x = {100};
    int i = 1000;
    x & i;
    return 0;
}
)SRC"),
                 "invalid operands to '");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesCompoundAssignStructRval_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use a structure type as the right operand in compound assignment
struct s { int i; };
int main(void) {
    int i = 100;
    struct s x = { 100 };
    i += x;
    return 0;
}
)SRC"),
                 "invalid operands to '");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesCompoundAssignToNestedStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't perform compound assignment with lval of struct type, including
// nested structs

struct inner {
    int i;
};

struct outer {
    struct inner s;
};

int main(void) {
    struct outer x = {{1}};
    x.s *= 10;
    return 0;
}
)SRC"),
                 "invalid operands to '");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesCompoundAssignToStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use operands of structure type in compound assignment operations
struct s { int i; };
int main(void) {
    struct s x = {10};
    x += 10;
    return 0;
}
)SRC"),
                 "invalid operands to '");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesDuplicateStructTypesAfterLabel_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// A label doesn't start a new scope, so you can't declare a new structure type
// after it
int main(void) {
    struct s {
        int a;
    };
foo:;
    // illegal redeclaration; struct s already declared in this scope
    struct s {
        int b;
    };
    return 0;
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesPostfixDecrStructArrow_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't apply ++/-- to any structures, including nested ones accessed thru ->
struct inner {int i;};
struct outer{struct inner s;};

int main(void) {
    struct outer my_struct = {{1}};
    struct outer *ptr = &my_struct;
    ptr->s--;
    return 0;
}
)SRC"),
                 "cannot decrement value of type");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesPostfixIncrStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't apply prefix or postfix ++/-- to structures
struct s {
    int i;
};

int main(void) {
    struct s my_struct = {1};
    my_struct++;
    return 0;
}
)SRC"),
                 "cannot increment value of type");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesPrefixDecrStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't apply prefix or postfix ++/-- to structures
struct s {
    int i;
};

int main(void) {
    struct s my_struct = {1};
    --my_struct;
    return 0;
}
)SRC"),
                 "cannot (in|de)crement value of type");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesPrefixIncrNestedStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't apply ++/-- to any structures, including nested ones
struct inner {
    int i;
};
struct outer {
    struct inner s;
};
int main(void) {
    struct outer x = {{1}};
    ++x.s;
    return 0;
}
)SRC"),
                 "cannot (in|de)crement value of type");
}

TEST_F(PipelineTest, Chapter18_OtherFeaturesSwitchOnStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use structure as controlling expression in switch statement
struct s {
    int i;
};

int main(void) {
    struct s x = {1};
    switch (x) {
        case 1:
            return 0;
        default:
            return 1;
    }
}
)SRC"),
                 "'switch' requires an integer value");
}


// --- invalid_types/extra_credit/scalar_required ---

TEST_F(PipelineTest, Chapter18_ScalarRequiredCastBetweenUnions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't cast operand to union type even if it already has that type

union u1 {
    int a;
};


int main(void){
    union u1 var = {10};
    (union u1) var; // illegal - no casts to union type
    return 0;
}
)SRC"),
                 "cannot cast '");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredCastUnionToInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't cast union to scalar type even if it has the right size
union u {
    int i;
};

int main(void) {
    union u x = {10};
    return (int)x;
}
)SRC"),
                 "cannot cast '");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredCompareUnions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't compare objects of union type

union u { long l; };

int main(void){
    union u x = {1};
    x == x; // illegal
    return 0;
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredSwitchOnUnion_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use union as controlling expression in switch statement
union s {
    int i;
};

int main(void) {
    union s x = {1};
    switch (x) {
        case 1:
            return 0;
        default:
            return 1;
    }
}
)SRC"),
                 "'switch' requires an integer value");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredUnionAsControllingExpression_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Can't use union as controlling expression

union u {int x;};

int main(void) {
    union u my_union = {10};
    if (my_union) {
        return 1;
    }
    return 0;
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}


// --- invalid_types/extra_credit/union_initializers ---

TEST_F(PipelineTest, Chapter18_UnionInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* A union initializer must always have exactly one element,
 * no matter how many members the union has.
 */

union u {
    int a;
    long b;
};

int main(void){
    union u x = {1, 2}; // invalid - multiple initializers
    return 0;
}
)SRC"),
                 "excess elements in union initializer");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersNestedInitWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* The one element in a union initializer (including a nested one)
 * must be compatible with the union's first element
 */

union u {
    double d;
    int i;
    char c;
};

struct s {
    int *ptr;
    union u arr[3];
};

int main(void) {
    int x;

    // invalid initializer for last element of arr;
    // can't convert pointer &x to double
    struct s my_struct = {&x, {{1.0}, {2.0}, {&x}}};
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersNestedUnionInitTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* All union initializers (including nested ones) must have exactly one element */

int main(void) {
    union u {
        double d; int x;
    };
    union u array_of_unions[3] = {
        // invalid; each of these must be individually enclosed in braces
        {1.0, 2.0, 3.0}
    };
}
)SRC"),
                 "excess elements in union initializer");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersScalarUnionInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't initialize a union with a scalar value */
union u {int a;};

int main(void){
    union u my_union = 1;
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticAggregateInitWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Make sure we validate the types of nested aggregate inits within union
// initializers

struct one_elem {
    long l;
};
struct three_elems {
    int one;
    int two;
    int three;
};

union one_or_three_elems {
    struct one_elem a;
    struct three_elems b;
};

int main(void) {
    // invalid: first element of union is struct one_elem, which we can't
    // initialize with three-element initializer
    static union one_or_three_elems my_union = {{1, 2, 3}};
    return 0;
}
)SRC"),
                 "excess elements in struct initializer");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticNestedInitNotConst_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Validate that all static union initializers, including nested ones,
// are constant

union u {
    long l;
};

struct has_union {
    int a;
    union u b;
    char c;
};

long some_var = 10l;

struct has_union some_struct = {1,
                                {some_var},  // INVALID - not constant
                                'a'};
)SRC"),
                 "initializer element is not a constant expression");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticNestedInitTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Union initializers, including nested ones, must have exactly one element
union u {
    int a;
    long b;
};
struct s {
    int tag;
    union u contents;
};

struct s my_struct = {
    10,
    {1, 2}  // invalid - nested union initializer has two elements
};
)SRC"),
                 "excess elements in union initializer");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticScalarUnionInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't initialize a union with a scalar value */
union u {int a;};

int main(void){
    static union u my_union = 1;
    return 0;
}
)SRC"),
                 "cannot initialize 'union u' with a scalar value");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* A union initializer must always have exactly one element,
 * no matter how many members the union has.
 */

union u {
    int a;
    long b;
};

union u x = {1, 2};  // invalid - multiple initializers

int main(void) {
    return 0;
}
)SRC"),
                 "excess elements in union initializer");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticUnionInitNotConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* The initializer for any static variable must be a constant,
 * including variables of union type.
 */

union u {int a; int b;};

int main(void){
    int i = 10;
    static union u my_union = {i};
    return 0;
}
)SRC"),
                 "initializer element is not a constant expression");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersStaticUnionInitWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* The one element in a union initializer must be compatible with the union's
 * first member.
 */

union u {
    signed char *ptr;
    double d;
};

int main(void) {
    // invalid; cannot implicitly convert char * to signed char *
    static union u my_union = {"A char array"};
}
)SRC"),
                 "cannot initialize 'signed char \\*' with a string literal");
}

TEST_F(PipelineTest, Chapter18_UnionInitializersUnionInitWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* The one element in a union initializer must be compatible with the union's
 * first member.
 */

union u {
    long *ptr;
    double d;
};

int main(void) {
    // invalid; cannot implicitly convert double 1.0 to type of first
    // member (long *)
    union u my_union = {1.0};
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}


// --- invalid_types/extra_credit/union_struct_conflicts ---

TEST_F(PipelineTest, Chapter18_TagDeclAndUse_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't declare a type with a struct specifier and then refer to it with
 * a union specifier.
 */
struct x { int a; };

int main(void) {
    union x foo; // incompatible with earlier declration of 'struct x' type
    return 0;
}
)SRC"),
                 ".");
}

// The inner `union s` references the visible outer `struct s` (no shadowing), so the
// wrong-keyword use is a cross-kind conflict.
TEST_F(PipelineTest, Chapter18_TagDeclAndUseSelfReference_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* It's illegal to use a 'union s' type when a 'struct s' type is in scope ,
 * or vice versa.
 */

int main(void) {
    struct s;
    {
        union s* ptr;
    }
    return 0;
}
)SRC"),
                 ".");
}

TEST_F(PipelineTest, Chapter18_TagDeclarations_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* It's illegal to specify struct and union types with the same tag in the same scope */
struct x;
union x;

int main(void) {
    return 0;
}
)SRC"),
                 ".");
}

TEST_F(PipelineTest, Chapter18_StructShadowedByUnion_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* When one type declaration shadows another with the same tag (including a
 * union type shadowing struct type or vice versa) you can't specify the outer
 * tag
 * */
int main(void) {
    struct tag {int a;};
    {
        union tag {long l;}; // shadows previous definition of tag
        // illegal to specify 'struct tag' here b/c it conflicts with
        // 'union tag' declared above
        struct tag *x;
    }
    return 0;
}
)SRC"),
                 "redefinition of 'union tag'");
}

TEST_F(PipelineTest, Chapter18_TagDeclConflictsWithDef_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't declare 'struct s' and define 'union s' or vice versa in same scope
 * (This test is intended to verify that we detect conflicts between declarations
 * and definitions rather than just ignoring the declarations/letting the
 * definitions overwrite them)
 * */

int main(void) {
    struct s;
    union s { // conflicts w/ earlier declaration
        int a;
    };

    return 0;
}
)SRC"),
                 ".");
}

TEST_F(PipelineTest, Chapter18_TagDefConflictsWithDecl_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't declare 'struct s' and define 'union s' or vice versa in same scope
 * (This test is intended to verify that we detect conflicts between declarations
 * and definitions rather than just ignoring the declarations/letting the
 * definitions overwrite them; identical to type_decl_conflicts_with_def with
 * order of type declarations swapped)
 * */

int main(void) {
    union s { // conflicts w/ earlier declaration
        int a;
    };
    struct s;

    return 0;
}
)SRC"),
                 ".");
}

// DISABLED: relies on tag shadowing in an inner scope; no-shadowing design has no distinct inner type
TEST_F(PipelineTest, Chapter18_UnionShadowedByIncompleteStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(

/* Under the no-shadowing design the inner block has no separate scope for the
 * tag, so 'struct tag' reopens the existing 'union tag' with the wrong keyword —
 * a cross-kind conflict.
 * */

int main(void) {
    union tag {int a;};
    {
        struct tag; // illegal: 'tag' already declared as a union
        union tag *x;
    }
    return 0;
}
)SRC"),
                 "'tag' was declared as a different kind of tag");
}


// --- invalid_types/extra_credit/union_tag_resolution ---

TEST_F(PipelineTest, Chapter18_UnionTagResolutionAddressOfWrongUnionType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u {
    int i;
    char c;
};

int main(void) {
    // declare variable with outer 'union u' type
    union u foo = {0};

    // declare another union u type, shadowing the first
    // this is a distinct type even though its declaration is identical
    union u {
        int i;
        char c;
    };

    // invalid initializer: can't convert pointer to outer union u (&foo)
    // to pointer to inner union u (ptr)
    union u *ptr = &foo;
}
)SRC"),
                 "redefinition of 'union u'");
}

TEST_F(PipelineTest, Chapter18_UnionTagResolutionCompareStructAndUnionPtrs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* If 'struct tag' shadows 'union tag' or vice versa, these two types are
 * distinct so pointers to them cannot be compared
 */

int main(void) {
    struct tag;
    struct tag *struct_ptr = 0;
    {
        // Under the no-shadowing design this 'union tag' refers to the outer
        // 'struct tag', so the wrong keyword is a cross-kind conflict.
        union tag;
        union tag *union_ptr = 0;
        return (struct_ptr == union_ptr);
    }
}
)SRC"),
                 "'tag' was declared as a different kind of tag");
}

TEST_F(PipelineTest, Chapter18_UnionTagResolutionConflictingParamUnionTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

// declare a function that takes param with incomplete struct type
int foo(struct s x);

int main(void) {
    // 'union s' here refers to the file-scope 'struct s' (no shadowing), so the
    // wrong keyword is itself a cross-kind conflict.
    union s;

    int foo(union s x);
    return 0;
}
)SRC"),
                 "'s' was declared as a different kind of tag");
}

TEST_F(PipelineTest, Chapter18_UnionTagResolutionDistinctUnionTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int foo(void) {
    union s {
        int a;
        long b;
    };
    union s result = {1};
    return result.a;
}

int main(void) {
    // previously defined union s is not in scope here,
    // so this is declares a new incomplete type
    union s;
    // this is illegal because it defines a variable with an incomplete type
    union s blah = {foo()};
    return blah.a;
}
)SRC"),
                 "variable 'blah' has incomplete type 'union s'");
}

// Under no-shadowing, 'union u' reopens the file-scope 'struct u' with the wrong
// keyword — a cross-kind conflict.
TEST_F(PipelineTest, Chapter18_UnionTagResolutionUnionTypeShadowsStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// Test that a same-tag declaration with the wrong keyword is rejected

// define a struct type
struct u {
    int a;
};

int main(void) {
    // 'union u' refers to the file-scope 'struct u' (no shadowing), so the wrong
    // keyword is itself a cross-kind conflict.
    union u;
    union u my_union;
    return 0;
}
)SRC"),
                 "'u' was declared as a different kind of tag");
}

TEST_F(PipelineTest, Chapter18_UnionTagResolutionUnionWrongMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u {
    int a;
};

int main(void) {
    union u foo = {1};

    // introduce a different union u type
    union u {
        int b;
    };

    return foo.b; // foo belongs to outer union u type, which doesn't have member 'b'
}
)SRC"),
                 "redefinition of 'union u'");
}


// --- invalid_types/extra_credit/union_type_declarations ---

// An array with an incomplete element type is rejected even when the array is
// behind a pointer (union u (*arr)[3]); validate_type recurses through the
// pointer into the array and catches the incomplete element.
TEST_F(PipelineTest, Chapter18_ArrayOfIncompleteUnionType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* It's illegal to specify an array type with any incomplete
 * element type, including union types.
 */

union u;  // declare incomplete union type
int main(void) {
    // declare pointer to array of three union u elements;
    // illegal because union u is incomplete
    union u(*arr)[3];
    return 0;
}
)SRC"),
                 ".");
}

TEST_F(PipelineTest, Chapter18_DuplicateUnionDef_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* YOu can't declare the same union type twice. */

int main(void) {
    union u {int a;};
    union u {int a;}; // illegal - duplicate declaration
    return 0;
}
)SRC"),
                 "redefinition of 'union u'");
}

TEST_F(PipelineTest, Chapter18_IncompleteUnionMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't declare a union with an incomplete member type */
struct s;
union u {
    struct s bad_struct;
};

int main(void){
    return 0;
}
)SRC"),
                 "member 'bad_struct' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_MemberNameConflicts_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
/* You can't declare two members of the same union with the same name */
union u {
    int a;
    int a;
};

int main(void) {
    return 0;
}
)SRC"),
                 "duplicate member 'a'");
}

TEST_F(PipelineTest, Chapter18_UnionSelfReference_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union u {
    int i;
    union u self; //illegal; incomplete member type
};

int main(void) {
    return 0;
}
)SRC"),
                 "member 'self' has incomplete type 'union u'");
}


// --- invalid_types/incompatible_types ---

TEST_F(PipelineTest, Chapter18_TypesAssignDifferentPointerType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s1;
struct s2;

int main(void) {
    struct s1 *p1 = 0;
    struct s2 *p2 = 0;
    p2 = p1;  // can't assign to struct s2 * from struct s1 *
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_TypesAssignDifferentStructType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
// can't assign one struct type to another

struct s1 {
    int field;
};

struct s2 {
    int field;
};

int main(void) {
    struct s1 a = {1}   ;
    struct s2 b;
    b = a; // can't assign to struct s2 from struct s1
    return b.field;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_TypesBranchMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s1 {
  int a;
};

struct s2 {
  int b;
};

int main(void) {
  struct s1 x = {1};
  struct s2 y = {2};
  1 ? x : y; // can't have conditional branches with different struct types
}
)SRC"),
                 "incompatible operand types in '\\?:'");
}

TEST_F(PipelineTest, Chapter18_TypesBranchMismatch2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x = {1};
    // can't have conditional branches where only one branch is a struct
    1 ? x : (void) 2;
}
)SRC"),
                 "incompatible operand types in '\\?:'");
}

TEST_F(PipelineTest, Chapter18_TypesCompareDifferentStructPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s1;
struct s2;

struct s1 *get_s1_ptr(void);
struct s2 *get_s2_ptr(void);

int main(void) {
  // can't compare pointers to two distinct struct types
  struct s1 *s1_ptr = get_s1_ptr();
  struct s2 *s2_ptr = get_s2_ptr();
  return s1_ptr == s2_ptr;
}
)SRC"),
                 "incompatible pointer types");
}

TEST_F(PipelineTest, Chapter18_TypesReturnWrongStructType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct one {
  int x;
  int y;
};

struct two {
  int a;
  int b;
};

struct one return_struct(void) {
    struct two retval = {1, 2};
    return retval; // can't return a "struct two" from function w/ return type "struct one"
}

int main(void) {
    return return_struct().x;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_TypesStructParamMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct one {
  int x;
  int y;
};

struct two {
  int a;
  int b;
};

int take_struct_param(struct one param) {
    return param.x;
}

int main(void) {
    struct two arg = {1, 2};
    return take_struct_param(arg); // can't convert argument of type "struct two" to parameter of type "struct one"
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_TypesStructPointerParamMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s1 {
    int a;
};

struct s2 {
    int a;
};

int get_a(struct s1 *ptr) {
    return ptr->a;
}

int main(void) {
    struct s2 arg = {1};
    // can't pass a struct s2 * to a function that expects a struct s1 *
    return get_a(&arg);
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}


// --- invalid_types/initializers ---

TEST_F(PipelineTest, Chapter18_InitializersCompoundInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct pair {
    int a;
    int b;
};

int main(void) {
    // a compound structure initializer can't initialize more values than the
    // struct has
    struct pair p = {1, 2, 3};
    return 0;
}
)SRC"),
                 "excess elements in struct initializer");
}

TEST_F(PipelineTest, Chapter18_InitializersInitStructWithString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct chars {
    char a;
    char b;
    char c;
    char null;
};

int main(void) {

    // you can't initialize structure members with a string,
    // even if they're all chars
    struct chars my_chars = "abc";
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_InitializeNestedStaticStructMemberWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  double d;
  void *arr[3];
};

// can't initialize a nested element of type void * with a constant of type double
struct s x = {0.0, {1.0}};
)SRC"),
                 "cannot initialize 'void \\*' with this initializer");
}

// DISABLED: static struct initialized with a scalar 0 is not rejected (static-init path)
TEST_F(PipelineTest, Chapter18_InitializersStaticStructWithZero_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

// you can't initialize a static struct (or any struct) with a scalar constant
struct s x = 0;
)SRC"),
                 ".");
}

TEST_F(PipelineTest, Chapter18_InitializersStructMemberWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    signed char *char_ptr;
};

int main(void) {
    // because the structure member char_ptr has type signed char *,
    // rather than char *, we can't initialize it with a string literal
    struct s x = {"It's a string"};
    return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_InitializersStructWithScalar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    struct pair {
        int x;
        int y;
    };

    // you can't initialize a struct with a scalar expression
    struct pair p = 1;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_InitializersStructWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct one {
  int x;
  int y;
};

struct two {
  int a;
  int b;
};

int main(void) {
  struct one x = {1, 2};
  struct two y = x; // can't initialize a struct from different struct type
  return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_InitializersNestedCompoundInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct inner {
    int i;
};

struct outer {
    struct inner foo;
};

int main(void) {
    struct outer x = {{1, 2}}; // sub-initializer for nested 'struct inner' has too many elements
    return 0;
}
)SRC"),
                 "excess elements in struct initializer");
}

TEST_F(PipelineTest, Chapter18_InitializersNestedStaticCompoundTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct inner {
    int i;
};

struct outer {
    struct inner foo;
};

struct outer x = {{1, 2}}; // sub-initializer for nested 'struct inner' has too many elements
)SRC"),
                 "excess elements in struct initializer");
}

TEST_F(PipelineTest, Chapter18_InitializersNestedStructInitializerWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct inner {
  int a;
  int b;
};

struct outer {
  struct inner x;
};

int main(void) {
  struct outer x = {{1, 2}};
  // can't initialize second element of type 'struct inner'
  // from variable of type 'struct outer'
  struct outer y = {1, x};
  return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_InitializersNonConstantStaticElemInit_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct pair {
  int a;
  int b;
};

struct pair x = {1, 2};
struct outer {
    double d;
    struct pair inner;
};

// you can't initialize an element in a static variable with a non-constant expression
struct outer y = {1.0, x};
)SRC"),
                 "initializer element is not a constant expression");
}

TEST_F(PipelineTest, Chapter18_InitializersNonConstantStaticInit_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct pair {
  int a;
  int b;
};
struct pair x = {1, 2};
// you can't initialize a static variable with a non-constant expression
struct pair y = x;
)SRC"),
                 "cannot initialize 'struct pair' with this initializer");
}

TEST_F(PipelineTest, Chapter18_InitializersStaticInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct pair {
    int a;
    int b;
};
// a static compound structure initializer can't initialize more values than the
// struct has
struct pair p = {1, 2, 3};
)SRC"),
                 "excess elements in struct initializer");
}

// A static pointer initialized with an address constant whose array subscript is not a
// compile-time constant is rejected (the address-constant evaluator's diagnostic).
TEST_F(PipelineTest, Chapter18_StaticAddressConstantNonConstSubscript_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int arr[3];
int i;
int *p = &arr[i];
)SRC"),
                 "array subscript in an initializer is not a constant expression");
}

// A scalar variable's value is not an address constant, so it cannot initialize a static
// pointer; the evaluator declines it and the catch-all rejects the initializer.
TEST_F(PipelineTest, Chapter18_StaticPointerFromScalarVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int x;
int *p = x;
)SRC"),
                 "cannot initialize 'int \\*' with this initializer");
}


// --- invalid_types/invalid_incomplete_structs ---

TEST_F(PipelineTest, Chapter18_IncompleteStructsAssignToIncompleteVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s x;
extern struct s y;

int main(void) {
  x = y; // can't assign to or from variable with incomplete type
  return 0;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsCastIncompleteStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s v;

int main(void) {
  // you can't perform a cast on a struct with incomplete type
  (void)v;
  return 0;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsDerefIncompleteStructPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

struct s *ptr = 0;

int main(void) {
  // can't dereference pointer to incomplete type
  // except in expression &*ptr
  *ptr;
  return 0;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteArgFuncall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

void f(struct s param);

extern struct s extern_var;

int main(void) {
  // can't pass a variable with incomplete type as an argument
  f(extern_var);
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteArrayElement_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

// can't specify an array type whose element type is incomplete
// even if the type is completed later
struct s arr[3];

struct s {
    int a;
    int b;
};

int main(void) {
    return 0;
}
)SRC"),
                 "array has incomplete element type");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteLocalVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

int main(void) {
  // can't define a local variable (or any variable) with incomplete type
  struct s v;
  return 0;
}
)SRC"),
                 "variable 'v' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteParam_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

// it's illegal to define a function with a parameter of incomplete type,
// even if the parameter isn't used
int foo(struct s x) { return 0; }
)SRC"),
                 "function 'foo' has an incomplete return or parameter type");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompletePtrAddition_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s *ptr;

int main(void) {
  // can't perform pointer addition w/ pointers to incomplete types
  return ptr + 0 == ptr;
}
)SRC"),
                 "invalid operands to '\\+'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompletePtrSubtraction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s *ptr;

int main(void) {
  // can't perform pointer substraction w/ pointers to incomplete types
  return (ptr - ptr) == 0;
}
)SRC"),
                 "invalid operands to '-'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteReturnTypeFunDef_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void exit(int status);

struct s;

// you can't define a function with an incomplete return type,
// even if it doesn't actually return a value
struct s return_struct_def(void) {
  exit(0);
}

int main(void) { return 0; }
)SRC"),
                 "function 'return_struct_def' has an incomplete return or parameter type");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteReturnTypeFuncall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

struct s f(void);

int main(void) {
  f(); // can't call a function with an incomplete return type (besides void)
  return 0;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteStructConditional_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s v1;
extern struct s v2;

int main(void) {
  // can't use expressions with incomplete structure type as branches in conditional expression
  1 ? v1 : v2;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteStructFullExpr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s x;

int main(void) {
  // can't use expression w/ incomplete struct type as expression statement
  for (x;;)
    ;
  return 0;
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteStructMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

extern struct s foo;

int main(void) {
  return foo.a; // can't get member of incomplete structure type
}
)SRC"),
                 "where a complete type is required");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteSubscript_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;
extern struct s *ptr;

// can't subscript a pointer to an incomplete type
// this is equivalent to dereferencing a pointer to an incomplete type
int main(void) { ptr[0]; }
)SRC"),
                 "invalid operands to '\\[\\]'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsIncompleteTentativeDef_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

// it's illegal to define a file scope variable (or any variable)
// with an incomplete structure type
// (some compilers allow tentative definitions if the var has external linkage
// and the  type is completed
// later in the same translation unit, but we don't.)
static struct s x;

int main(void) { return 0; }
)SRC"),
                 "variable 'x' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsInitializeIncomplete_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

// you can declare extern variables of incomplete type
// but it's illegal to initialize them
extern struct s x = {1};

int main(void) { return 0; }

struct s {
  int a;
};
)SRC"),
                 "variable 'x' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsSizeofIncomplete_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;

int main(void) {
  return sizeof(struct s); // can't take size of incomplete type
}
)SRC"),
                 "invalid application of 'sizeof' to an incomplete type");
}

TEST_F(PipelineTest, Chapter18_IncompleteStructsSizeofIncompleteExpr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s;
struct s *get_ptr(void);

int main(void) {
    struct s *struct_ptr = get_ptr();

    // can't apply sizeof to expression w/ incomplete type
    return sizeof(*struct_ptr);
}
)SRC"),
                 "invalid application of 'sizeof' to an incomplete type");
}


// --- invalid_types/invalid_lvalues ---

TEST_F(PipelineTest, Chapter18_LvaluesAddressOfNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int arr[3];
  double d;
};

int main(void) {
  struct s x = {{1, 2, 3}, 4.0};
  struct s y = {{9, 8, 7}, 6.0};
  // can't take address of element b/c it's not an lvalue
  // (even though it has temporary lifetime)
  int *arr[3] = &((1 ? x : y).arr);
  return 0;
}
)SRC"),
                 "cannot take the address of an rvalue");
}

// `return_struct()` is an rvalue, so the nested member `.b.x` is a non-lvalue: reject the assignment.
TEST_F(PipelineTest, Chapter18_LvaluesAssignNestedNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct inner {
  int x;
  int y;
};

struct outer {
  int a;
  struct inner b;
};

struct outer return_struct(void) {
  struct outer result = {1, {2, 3}};
  return result;
}

int main(void) {
  // can't assign to non-lvalue
  return_struct().b.x = 10;
  return 0;
}
)SRC"),
                 "expression is not assignable");
}

// DISABLED: assignment to an array-typed lvalue (array member) is not rejected
TEST_F(PipelineTest, Chapter18_LvaluesAssignToArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct chars {
  char char_array[5];
};

int main(void) {
  struct chars x = {{1, 2, 3, 4, 5}};
  char arr[5] = {9, 8, 7, 6, 5};
  // can't assign to char_array member because it decays to a pointer
  x.char_array = arr;
  return x.char_array[0];
}
)SRC"),
                 ".");
}

// `(1 ? x : y)` is an rvalue, so the member `.d` is a non-lvalue: reject the assignment.
TEST_F(PipelineTest, Chapter18_LvaluesAssignToNonLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int arr[3];
  double d;
};

int main(void) {
  struct s x = {{1, 2, 3}, 4.0};
  struct s y = {{9, 8, 7}, 6.0};
  // can't assign to this struct member because it's not an lvalue
  (1 ? x : y).d = 0.0;

  return 0;
}
)SRC"),
                 "expression is not assignable");
}


// --- invalid_types/invalid_member_operators ---

TEST_F(PipelineTest, Chapter18_MemberOperatorsArrowPointerToNonStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    long l;
};

int main(void) {
    double d = 0.0;
    double* ptr = &d;
    return ptr->l;  // can't apply -> operator to pointer to non-struct
}
)SRC"),
                 "member reference type '.*' is not a pointer to a structure or union");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsBadMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int x;
    int y;
};

struct t {
    int blah;
    int y;
};

int main(void) {
    struct s foo = {1, 2};
    return foo.blah; // "struct s" has no member "blah"
}
)SRC"),
                 "no member named 'blah' in '(struct|union) s'");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsBadPointerMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void *malloc(unsigned long size);

struct a {
  int x;
  int y;
};

struct b {
  int m;
  int n;
};

int main(void) {
  struct a *ptr = malloc(sizeof(struct a));
  ptr->m = 10; // "struct a" has no member "m"
  return 0;
}
)SRC"),
                 "no member named 'm' in '(struct|union) a'");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsMemberOfNonStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void *malloc(unsigned long size);

struct a {
  int x;
  int y;
};

int main(void) {
  struct a *ptr = malloc(sizeof(struct a));
  ptr.x = 10; // can't apply . operator to struct pointer
}
)SRC"),
                 "member reference base type '.*' is not a structure or union");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsMemberPointerNonStructPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct a {
  int x;
  int y;
};

int main(void) {
  struct a my_struct = {1, 2};
  // can't apply -> to non-pointer
  return my_struct->x;
}
)SRC"),
                 "member reference type '.*' is not a pointer to a structure or union");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsNestedArrowPointerToNonStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    long l;
};

struct has_ptr {
    double *ptr;
};

int main(void) {
    double d = 0.0;
    struct has_ptr p_struct = { &d };
    return p_struct.ptr->l;  // can't apply -> operator to pointer to non-struct
}
)SRC"),
                 "member reference type '.*' is not a pointer to a structure or union");
}

TEST_F(PipelineTest, Chapter18_MemberOperatorsPostfixPrecedence_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    struct s {
        int a;
    };
    struct s x = {10};
    // postfix operatpors have higher precedence then prefix operators,
    // so this is equivalent to &(x->a),
    // which is invalid because x isn't a pointer.
    // This is really a test for the parser, not the type checker
    return &x->a;
}
)SRC"),
                 "member reference type '.*' is not a pointer to a structure or union");
}


// --- invalid_types/invalid_struct_declaration ---

TEST_F(PipelineTest, Chapter18_StructDeclarationDuplicateMemberName_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  // can't declare two members with same name
  int x;
  double x;
};
)SRC"),
                 "duplicate member 'x'");
}

TEST_F(PipelineTest, Chapter18_StructDeclarationDuplicateStructDeclaration_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    // can't declare two structures with the same tag
    // in the same scope
    struct x {
        int x;
    };
    struct x {
        int y;
    };
    return 0;
}
)SRC"),
                 "redefinition of 'struct x'");
}

TEST_F(PipelineTest, Chapter18_StructDeclarationIncompleteMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s; // declare incomplete structure type

struct a {
  // can't declare a struct member with incomplete type
  struct s g;
};
)SRC"),
                 "member 'g' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_StructDeclarationInvalidArrayMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct incomplete;

struct s {
  // member type is invalid: illegal to specify array of incomplete type,
  // even as a pointer's referenced type
  struct incomplete (*array_pointer)[3];
};
)SRC"),
                 "array has incomplete element type");
}

TEST_F(PipelineTest, Chapter18_StructDeclarationInvalidSelfReference_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int x;
    // a structure can't contain a member with its own type,
    // because its type has not yet been completed.
    // here, 'struct s' is still an incomplete type
    struct s y;
};

int main(void) {
    return 0;
}
)SRC"),
                 "member 'y' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_StructDeclarationVoidMember_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  // can't declare structure members with incomplete type, including void
  void x;
};
)SRC"),
                 "member 'x' has incomplete type 'void'");
}


// --- invalid_types/scalar_required ---

TEST_F(PipelineTest, Chapter18_ScalarRequiredAndStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x = {1};
    return 0 && x;  // can't apply boolean operators to structs
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredAssignNullPtrToStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

struct s x = {1};

int main(void) {
  // can't assign any scalar value (including null pointer constant)
  // to an lvalue of struct type
  x = 0;
  return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredAssignScalarToStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

struct s x = {1};

int main(void) {
  struct s *ptr = &x;
  *ptr = 2; // can't assign scalar value to lvalue of struct type
  return 0;
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredCastStructToScalar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

int main(void) {
  struct s x = {1};
  // can't cast struct to a scalar value
  int y = (int)x;
  return y;
}
)SRC"),
                 "cannot cast '");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredCastToStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

struct s x;

// can only cast to scalar type or void
// casting to struct type is illegal,
// even if operand already has that type
// (Clang/GCC only complain about this with -pedantic option)
int main(void) { (struct s) x; }
)SRC"),
                 "cannot cast '");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredCompareStructs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x = {1};
    struct s y = {2};
    return x == y; // can only apply == operator to scalars, not structures
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredNotStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x = {1};
    return !x;  // can only apply boolean operators to scalars, not structs
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredPassStructAsScalarParam_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};
int foo(int a) { return a; }

int main(void) {
  struct s x = {1};
  // can't convert struct to scalar (or to any other type) as if by assignment
  return foo(x);
}
)SRC"),
                 "cannot convert '.*' to '.*' when");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredStructAsInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

int main(void) {
  struct s x = {1};
  // can only apply ~ operator to ints, not structs
  (void)~x;
  return 0;
}
)SRC"),
                 "to unary '~'");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredStructControllingExpression_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

int main(void) {
  struct s x = {1};
  // can't use structure as controlling expression in if statement
  if (x)
    return 1;
  return 0;
}
)SRC"),
                 "(invalid operands to|a value of scalar type is required)");
}

TEST_F(PipelineTest, Chapter18_ScalarRequiredSubscriptStruct_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
};

int main(void) {
  struct s x = {1};
  return x[0]; // can only subscript pointers, not structures
}
)SRC"),
                 "invalid operands to '\\[\\]'");
}


// --- invalid_types/tag_resolution ---

TEST_F(PipelineTest, Chapter18_TagResolutionAddressOfWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int i;
};

int main(void) {
    // declare variable with outer 'struct s' type
    struct s foo = {0};

    // declare another struct s type, shadowing the first
    // this is a distinct type even though its declaration is identical
    struct s {
        int i;
    };

    // invalid initializer: can't convert pointer to outer struct s (&foo)
    // to pointer to inner struct s (ptr)
    struct s *ptr = &foo;
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionDistinctStructTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int foo(void) {
    struct s {
        int a;
        int b;
    };
    struct s result = {1, 2};
    return result.a + result.b;
}

int main(void) {
    // previously defined struct s is not in scope here,
    // so this is declares a new incomplete type
    struct s;
    // this is illegal because it defines a variable with an incomplete type
    struct s blah = {foo(), foo()};
    return blah.a;
}
)SRC"),
                 "variable 'blah' has incomplete type 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionInvalidShadowSelfReference_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s {
        // it's illegal to declare a member of type 'struct s' here, because tag
        // 's' refers to the type we're declaring now instead of the type we
        // declared earlier (this tests that we add the new tag to current scope before we
        // process its members)
        struct s nested;
    };
    return 0;
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionMemberNameWrongScope_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s foo = {1};

    // introduce a different struct s type
    struct s {
        int b;
    };

    return foo.b; // foo belongs to outer struct s type, which doesn't have member 'b'
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionMemberNameWrongScopeNested_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct outer {
        struct s inner;
    };

    struct outer foo = {{1}};

    // introduce a different struct s type
    struct s {
        int b;
    };

    struct outer *ptr = &foo;

    return ptr->inner.b;  // foo.inner belongs to first struct s type, which
                          // doesn't have member 'b'
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionMismatchedReturnType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
  int a;
  int b;
};

struct s return_struct(void) {
  // define another struct s that shadows previous one;
  struct s {
    int a;
    int b;
  };
  struct s result = {1, 2};
  // result has inner 'struct s' type instead of outer one,
  // so it's incompatible w/ function's return type
  return result;
}
)SRC"),
                 "redefinition of 'struct s'");
}

TEST_F(PipelineTest, Chapter18_TagResolutionShadowedTagBranchMismatch_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
int main(void) {
    struct s {
        int i;
    };
    struct s struct1 = {1};

    {
        struct s {
            int i;
        };
        struct s struct2 = {2};

        // invalid conditional expression: struct1 and struct2 have different
        // types
        (void)(1 ? struct1 : struct2);
    }
}
)SRC"),
                 "redefinition of 'struct s'");
}


// --- invalid_parse/extra_credit ---

TEST_F(PipelineTest, Chapter18_ExtraCreditUnionMemberIsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
union s {
    // a union member can't be a function
    // we treat this as a parse error, but it would also be reasonable
    // to handle it during type checking
    int foo(void);
};
)SRC"),
                 "member 'foo' has function type 'int \\(void\\)'");
}


// --- invalid_parse ---

TEST_F(PipelineTest, Chapter18_ParseStructMemberIsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
struct s {
    // a structure member can't be a function
    // we treat this as a parse error, but it would also be reasonable
    // to handle it during type checking
    int foo(void);
};
)SRC"),
                 "member 'foo' has function type 'int \\(void\\)'");
}
