#pragma once

#include <DirectXMath.h>

class Color
{
public:
	Color() : m_Value(DirectX::g_XMOne){}
	Color(float r, float g, float b, float a = 1.0f);

	float R() const { return DirectX::XMVectorGetX(m_Value); }
	float G() const { return DirectX::XMVectorGetY(m_Value); }
	float B() const { return DirectX::XMVectorGetZ(m_Value); }
	float A() const { return DirectX::XMVectorGetW(m_Value); }


private:
	DirectX::XMVECTORF32 m_Value;
};

inline Color::Color(float r, float g, float b, float a)
{
	m_Value.v = DirectX::XMVectorSet(r, g, b, a);
}
