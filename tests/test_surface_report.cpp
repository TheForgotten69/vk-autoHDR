#include "surface_report.hpp"
#include "test_util.hpp"

#include <cassert>
#include <cstring>

using namespace autohdr;

static surface_report hdr_report()
{
	surface_report r;
	r.tf_named = 11;
	r.max_lum = 1000;
	r.reference_lum = 455;
	r.target_max_lum = 455.5;
	return r;
}

int main()
{
	const char *why = nullptr;

	{
		const auto v = resolve_surface(hdr_report(), &why);
		assert(v);
		assert(near(v->reference_white_nits, 455, 1e-9));
		assert(near(v->peak_nits, 455.5, 1e-9));
		assert(!v->sdr_output);
	}
	{
		surface_report r;
		r.tf_named = 2;
		r.max_lum = 80;
		r.reference_lum = 80;
		r.target_max_lum = 80;
		r.target_max_cll = 80;
		const auto v = resolve_surface(r, &why);
		assert(v && v->sdr_output && near(v->reference_white_nits, 80, 1e-9) && near(v->peak_nits, 80, 1e-9));
	}
	{
		surface_report r;
		r.tf_named = 2;
		r.reference_lum = 317;
		r.target_max_lum = 450;
		const auto v = resolve_surface(r, &why);
		assert(v && !v->sdr_output);
	}
	{
		surface_report r = hdr_report();
		r.reference_lum.reset();
		why = nullptr;
		assert(!resolve_surface(r, &why));
		assert(why && std::strlen(why) > 0);
		assert(!resolve_surface(r, nullptr));
	}
	{
		surface_report r = hdr_report();
		r.target_max_lum = 400;
		why = nullptr;
		assert(!resolve_surface(r, &why) && why);
	}
	{
		surface_report r = hdr_report();
		r.reference_lum = 9.9;
		assert(!resolve_surface(r, &why));
		r.reference_lum = 10;
		r.target_max_lum = 10;
		assert(resolve_surface(r, &why));
		r.reference_lum = 10001;
		assert(!resolve_surface(r, &why));
		r = hdr_report();
		r.target_max_lum = 10000;
		assert(resolve_surface(r, &why));
		r.target_max_lum = 10001;
		assert(!resolve_surface(r, &why));
	}
	{
		surface_report r;
		r.reference_lum = 200;
		r.max_lum = 600;
		r.target_max_cll = 800;
		assert(near(resolve_surface(r, &why)->peak_nits, 800, 1e-9));
		r.target_max_cll.reset();
		assert(near(resolve_surface(r, &why)->peak_nits, 600, 1e-9));
		r.target_max_lum = 700;
		assert(near(resolve_surface(r, &why)->peak_nits, 700, 1e-9));
		r.target_max_lum.reset();
		r.max_lum.reset();
		why = nullptr;
		assert(!resolve_surface(r, &why) && why);
	}
	return 0;
}
