#include "advice.hpp"
#include <cassert>
#include <utility>
#include <cstring>

using namespace autohdr;

static technique_status on(int order) { return {true, true, order}; }
static technique_status off(int order) { return {true, false, order}; }
static technique_status missing() { return {}; }

static bool has(const std::vector<advice> &v, advice a)
{
	for (const advice x : v)
		if (x == a)
			return true;
	return false;
}

static technique_snapshot good()
{
	technique_snapshot t;
	t.deband = on(0);
	t.ui_capture = on(1);
	t.pumbo = on(2);
	t.ui_apply = on(3);
	return t;
}

int main()
{
	assert(advise(good(), true, 1).empty());

	{
		technique_snapshot t = good();
		t.pumbo = missing();
		assert(has(advise(t, true, 1), advice::pumbo_not_installed));
		assert(!has(advise(t, true, 1), advice::enable_pumbo));
		assert(!has(advise(t, false, 1), advice::pumbo_not_installed));
		t.pumbo = off(2);
		assert(has(advise(t, true, 1), advice::enable_pumbo));
		assert(!has(advise(t, false, 1), advice::enable_pumbo));
		assert(!has(advise(t, true, 1), advice::pumbo_not_installed));
	}

	{
		technique_snapshot t = good();
		assert(has(advise(t, false, 1), advice::pumbo_on_sdr));
		assert(!has(advise(t, true, 1), advice::pumbo_on_sdr));
		t.pumbo = off(2);
		assert(!has(advise(t, false, 1), advice::pumbo_on_sdr));
		t.pumbo = missing();
		assert(!has(advise(t, false, 1), advice::pumbo_on_sdr));
	}

	{
		technique_snapshot t = good();
		t.ui_capture = off(1);
		assert(has(advise(t, true, 2), advice::enable_ui_techniques));
		assert(!has(advise(t, true, 0), advice::enable_ui_techniques));
		assert(!has(advise(t, false, 2), advice::enable_ui_techniques));
		t = good();
		t.ui_apply = missing();
		assert(has(advise(t, true, 1), advice::enable_ui_techniques));
		assert(!has(advise(good(), true, 1), advice::enable_ui_techniques));
	}

	{
		technique_snapshot t = good();
		assert(has(advise(t, true, 0), advice::ui_techniques_without_masks));
		assert(!has(advise(t, true, 1), advice::ui_techniques_without_masks));
		t.ui_capture = off(1);
		t.ui_apply = off(3);
		assert(!has(advise(t, true, 0), advice::ui_techniques_without_masks));
		t.ui_apply = on(3);
		assert(has(advise(t, true, 0), advice::ui_techniques_without_masks));
	}

	{
		technique_snapshot t = good();
		std::swap(t.pumbo.order, t.ui_capture.order);
		assert(has(advise(t, true, 1), advice::wrong_order));
		t = good();
		std::swap(t.pumbo.order, t.ui_apply.order);
		assert(has(advise(t, true, 1), advice::wrong_order));
		t = good();
		t.deband.order = 5;
		assert(has(advise(t, true, 1), advice::wrong_order));
		t.deband.order = 2;
		t.pumbo.order = 3;
		t.ui_apply.order = 4;
		assert(has(advise(t, true, 1), advice::wrong_order));
		t = good();
		t.ui_capture = off(9);
		assert(!has(advise(t, true, 1), advice::wrong_order));
		t = good();
		t.ui_capture = missing();
		t.ui_apply.order = 0;
		assert(has(advise(t, true, 1), advice::wrong_order));
		t = good();
		t.ui_apply = off(0);
		assert(!has(advise(t, true, 1), advice::wrong_order));
		t.deband = off(7);
		assert(!has(advise(t, true, 1), advice::wrong_order));
		t.deband = missing();
		assert(!has(advise(t, true, 1), advice::wrong_order));
		technique_snapshot only;
		only.pumbo = on(4);
		assert(!has(advise(only, true, 0), advice::wrong_order));
	}

	{
		technique_snapshot t = good();
		t.deband = off(0);
		assert(has(advise(t, true, 1), advice::enable_deband));
		assert(!has(advise(t, false, 1), advice::enable_deband));
		t.deband = missing();
		assert(!has(advise(t, true, 1), advice::enable_deband));
		t = good();
		t.deband = off(0);
		t.pumbo = off(2);
		assert(!has(advise(t, true, 1), advice::enable_deband));
	}

	{
		technique_snapshot t = good();
		t.pumbo = off(2);
		t.deband = off(0);
		t.ui_capture = off(1);
		const std::vector<advice> v = advise(t, true, 1);
		for (size_t i = 1; i < v.size(); ++i)
			assert(static_cast<int>(v[i - 1]) < static_cast<int>(v[i]));
	}

	assert(is_warning(advice::pumbo_on_sdr) && is_warning(advice::wrong_order) &&
	       is_warning(advice::pumbo_not_installed) && is_warning(advice::enable_pumbo));
	assert(!is_warning(advice::enable_ui_techniques) && !is_warning(advice::ui_techniques_without_masks) &&
	       !is_warning(advice::enable_deband));
	for (int i = 0; i <= static_cast<int>(advice::enable_deband); ++i)
		assert(std::strlen(advice_text(static_cast<advice>(i))) > 10);
	return 0;
}
