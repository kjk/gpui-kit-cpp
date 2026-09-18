#include "Test.h"

#include "quickjs/quickjs.h"

#include <string.h>

static JSValue Eval(JSContext* ctx, const char* source,
                    int flags = JS_EVAL_TYPE_GLOBAL) {
    return JS_Eval(ctx, source, strlen(source), "quickjs-test.js", flags);
}

void TestQuickJs() {
    TestSuite("quickjs");

    JSRuntime* runtime = JS_NewRuntime();
    utassert(runtime != nullptr);
    if (!runtime) {
        return;
    }

    JSContext* context = JS_NewContext(runtime);
    utassert(context != nullptr);
    if (!context) {
        JS_FreeRuntime(runtime);
        return;
    }

    JSValue value =
        Eval(context, "[1, 2, 3].map(v => v * 2).reduce((a, b) => a + b, 0)");
    utassert(!JS_IsException(value));
    int32_t number = 0;
    utassert(JS_ToInt32(context, &number, value) == 0);
    utassert(number == 12);
    JS_FreeValue(context, value);

    JSValue module =
        Eval(context, "export const answer = 42", JS_EVAL_TYPE_MODULE);
    utassert(!JS_IsException(module));
    JS_FreeValue(context, module);

    JSValue promise =
        Eval(context, "Promise.resolve(40).then(value => value + 2)");
    utassert(!JS_IsException(promise));
    utassert(JS_PromiseState(context, promise) == JS_PROMISE_PENDING);
    JSContext* jobContext = nullptr;
    utassert(JS_ExecutePendingJob(runtime, &jobContext) > 0);
    utassert(jobContext == context);
    utassert(JS_PromiseState(context, promise) == JS_PROMISE_FULFILLED);
    JSValue result = JS_PromiseResult(context, promise);
    number = 0;
    utassert(JS_ToInt32(context, &number, result) == 0);
    utassert(number == 42);
    JS_FreeValue(context, result);
    JS_FreeValue(context, promise);

    JSValue exception = Eval(context, "function (");
    utassert(JS_IsException(exception));
    JSValue error = JS_GetException(context);
    utassert(JS_IsError(error));
    JS_FreeValue(context, error);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}
