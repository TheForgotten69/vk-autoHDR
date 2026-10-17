// Capture runs before AdvancedAutoHDR, Apply after it: inside the masks, Apply restores the pre-Pumbo pixels.
#include "ReShade.fxh"

uniform float4 AutoHDR_UiMasks[8] < hidden = true; >;
uniform int AutoHDR_UiMaskCount < hidden = true; >;
uniform float AutoHDR_UiWhiteScale < hidden = true; >;
uniform bool AutoHDR_ShowMaskOutlines < hidden = true; >;

texture AutoHDR_UiCaptureTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
sampler AutoHDR_UiCaptureSampler
{
	Texture = AutoHDR_UiCaptureTex;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = POINT;
};

static const int AUTOHDR_MAX_MASKS = 8;

float AutoHDR_SrgbEncode1(float x)
{
	return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 AutoHDR_SrgbEncode(float3 x)
{
	return float3(AutoHDR_SrgbEncode1(x.r), AutoHDR_SrgbEncode1(x.g), AutoHDR_SrgbEncode1(x.b));
}

float3 AutoHDR_Fix(float3 c)
{
	const float3 extra = c - saturate(c);
	float3 s = saturate(c);
	s = pow(AutoHDR_SrgbEncode(s), 2.2);
	return s + extra;
}

bool AutoHDR_InMask(float4 m, float2 uv)
{
	return uv.x >= m.x && uv.x < m.z && uv.y >= m.y && uv.y < m.w;
}

float4 AutoHDR_CapturePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	[loop]
	for (int i = 0; i < AUTOHDR_MAX_MASKS; ++i)
	{
		if (i >= AutoHDR_UiMaskCount)
			break;
		if (AutoHDR_InMask(AutoHDR_UiMasks[i], uv))
			return tex2D(ReShade::BackBuffer, uv);
	}
	discard;
}

float4 AutoHDR_ApplyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
	const float4 back = tex2D(ReShade::BackBuffer, uv);
	const float2 px = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
	const float2 border = 2.0 * px;
	bool outline = false;

	[loop]
	for (int i = 0; i < AUTOHDR_MAX_MASKS; ++i)
	{
		if (i >= AutoHDR_UiMaskCount)
			break;

		const float4 m = AutoHDR_UiMasks[i];
		if (AutoHDR_InMask(m, uv))
		{
			const float3 captured = tex2D(AutoHDR_UiCaptureSampler, uv).rgb;
			return float4(AutoHDR_Fix(captured) * AutoHDR_UiWhiteScale, back.a);
		}

		if (AutoHDR_ShowMaskOutlines
			&& uv.x >= m.x - border.x && uv.x < m.z + border.x
			&& uv.y >= m.y - border.y && uv.y < m.w + border.y)
		{
			outline = true;
		}
	}

	if (outline)
		return float4(float3(1.0, 0.0, 1.0) * AutoHDR_UiWhiteScale, back.a);
	return back;
}

technique AutoHDR_UI_Capture
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = AutoHDR_CapturePS;
		RenderTarget = AutoHDR_UiCaptureTex;
		ClearRenderTargets = false;
	}
}

technique AutoHDR_UI_Apply
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = AutoHDR_ApplyPS;
	}
}
