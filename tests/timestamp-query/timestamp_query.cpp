// Timestamp queries around N full-texture copies in one command buffer, against
// the CPU-observed completion time of an event query.
#include <d3d11.h>
#include <cstdio>
#include <initializer_list>
#include <windows.h>

static double
Ms(LARGE_INTEGER a, LARGE_INTEGER b)
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

template <typename T>
static void
Wait(ID3D11DeviceContext *ctx, ID3D11Query *q, T *data)
{
	while (ctx->GetData(q, data, sizeof(T), 0) != S_OK) {
	}
}

int
main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	ID3D11Device *dev;
	ID3D11DeviceContext *ctx;
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &dev,
	                             nullptr, &ctx))) {
		printf("FAIL device\n");
		return 1;
	}
	D3D11_TEXTURE2D_DESC td = {4096, 4096, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, 0, 0, 0};
	ID3D11Texture2D *a, *b, *rt;
	dev->CreateTexture2D(&td, nullptr, &a);
	dev->CreateTexture2D(&td, nullptr, &b);
	D3D11_TEXTURE2D_DESC rd = td;
	rd.Width = rd.Height = 8192;
	rd.BindFlags = D3D11_BIND_RENDER_TARGET;
	dev->CreateTexture2D(&rd, nullptr, &rt);
	ID3D11RenderTargetView *rtv;
	dev->CreateRenderTargetView(rt, nullptr, &rtv);
	const bool clears = GetEnvironmentVariableA("TSQ_CLEARS", nullptr, 0) > 0;
	printf("work: %s\n", clears ? "8192x8192 render-target clears" : "4096x4096 copies");
	D3D11_QUERY_DESC dj = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, ts = {D3D11_QUERY_TIMESTAMP, 0}, ev = {D3D11_QUERY_EVENT, 0};
	int failures = 0;
	struct Case { int before, copies; };
	for (Case c : {Case{0, 0}, Case{0, 0}, Case{0, 10}, Case{0, 40}, Case{4, 10}, Case{4, 40}, Case{4, 0}}) {
		int copies = c.copies;
		ID3D11Query *qd, *q0, *q1, *qe, *qw;
		dev->CreateQuery(&dj, &qd);
		dev->CreateQuery(&ts, &q0);
		dev->CreateQuery(&ts, &q1);
		dev->CreateQuery(&ev, &qe);
		dev->CreateQuery(&ev, &qw);
		// Drain earlier work so the event time measures only this batch.
		ctx->End(qw);
		ctx->Flush();
		BOOL done;
		Wait(ctx, qw, &done);
		// Work already queued in the same command buffer before the first query,
		// as in a game frame that does not flush before timing.
		for (int i = 0; i < c.before; ++i) {
			if (clears) {
				float colour[4] = {i * 0.1f, 0, 0, 1};
				ctx->ClearRenderTargetView(rtv, colour);
			} else {
				ctx->CopyResource((i & 1) ? a : b, (i & 1) ? b : a);
			}
		}
		ctx->Begin(qd);
		ctx->End(q0);
		for (int i = 0; i < copies; ++i) {
			if (clears) {
				float colour[4] = {0, i * 0.1f, 0, 1};
				ctx->ClearRenderTargetView(rtv, colour);
			} else {
				ctx->CopyResource((i & 1) ? a : b, (i & 1) ? b : a);
			}
		}
		ctx->End(q1);
		ctx->End(qd);
		ctx->End(qe);
		LARGE_INTEGER t0, t1;
		QueryPerformanceCounter(&t0);
		ctx->Flush();
		Wait(ctx, qe, &done);
		QueryPerformanceCounter(&t1);
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d;
		UINT64 v0, v1;
		Wait(ctx, qd, &d);
		Wait(ctx, q0, &v0);
		Wait(ctx, q1, &v1);
		double gpu = (double)(v1 - v0) * 1000.0 / (double)d.Frequency;
		double cpu = Ms(t0, t1);
		bool ok = v1 >= v0 && !d.Disjoint && (copies == 0 ? gpu < 2.0 : gpu > 0.5 * cpu * copies / (copies + c.before) && gpu <= cpu + 1.0);
		failures += !ok;
		printf("%s before=%d copies=%2d timestamp delta %.3f ms, flush-to-event %.3f ms, frequency %llu\n", ok ? "ok  " : "FAIL",
		       c.before, copies, gpu, cpu, (unsigned long long)d.Frequency);
		qd->Release(), q0->Release(), q1->Release(), qe->Release(), qw->Release();
	}
	// Deferred contexts (as Source 2 renders): queries recorded around the work
	// in a command list, executed on the immediate context.
	ID3D11DeviceContext *def;
	if (FAILED(dev->CreateDeferredContext(0, &def))) {
		printf("FAIL deferred context\n");
		return 1;
	}
	for (int copies : {0, 10, 40}) {
		ID3D11Query *qd, *q0, *q1, *qe, *qw;
		dev->CreateQuery(&dj, &qd);
		dev->CreateQuery(&ts, &q0);
		dev->CreateQuery(&ts, &q1);
		dev->CreateQuery(&ev, &qe);
		dev->CreateQuery(&ev, &qw);
		ctx->End(qw);
		ctx->Flush();
		BOOL done;
		Wait(ctx, qw, &done);
		def->End(q0);
		for (int i = 0; i < copies; ++i) {
			if (clears) {
				float colour[4] = {0, 0, i * 0.1f, 1};
				def->ClearRenderTargetView(rtv, colour);
			} else {
				def->CopyResource((i & 1) ? a : b, (i & 1) ? b : a);
			}
		}
		def->End(q1);
		ID3D11CommandList *list;
		def->FinishCommandList(FALSE, &list);
		ctx->Begin(qd);
		ctx->ExecuteCommandList(list, FALSE);
		ctx->End(qd);
		ctx->End(qe);
		LARGE_INTEGER t0, t1;
		QueryPerformanceCounter(&t0);
		ctx->Flush();
		Wait(ctx, qe, &done);
		QueryPerformanceCounter(&t1);
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d;
		UINT64 v0, v1;
		Wait(ctx, qd, &d);
		Wait(ctx, q0, &v0);
		Wait(ctx, q1, &v1);
		double gpu = (double)(v1 - v0) * 1000.0 / (double)d.Frequency;
		double cpu = Ms(t0, t1);
		bool ok = v1 >= v0 && !d.Disjoint && (copies == 0 ? gpu < 2.0 : gpu > 0.5 * cpu && gpu <= cpu + 1.0);
		failures += !ok;
		printf("%s deferred copies=%2d timestamp delta %.3f ms, flush-to-event %.3f ms\n", ok ? "ok  " : "FAIL",
		       copies, gpu, cpu);
		list->Release(), qd->Release(), q0->Release(), q1->Release(), qe->Release(), qw->Release();
	}
	printf("%s timestamp queries: failures=%d\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
