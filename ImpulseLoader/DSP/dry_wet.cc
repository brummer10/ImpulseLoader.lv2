// generated from file '/home/brummer/projecte/XUiDesigner/tools/wet_dry.dsp' by dsp2cc:
// Code generated with Faust 2.69.3 (https://faust.grame.fr)

#include <cmath>

#define FAUSTFLOAT float

namespace wet_dry {

class Dsp {
private:
	uint32_t fSampleRate;
	FAUSTFLOAT fVslider0;
	FAUSTFLOAT	*fVslider0_;


public:
	void connect(uint32_t port,void* data);
	void del_instance(Dsp *p);
	void init(uint32_t sample_rate);
	void compute(int count, FAUSTFLOAT *input0, FAUSTFLOAT *input1, FAUSTFLOAT *output0);
	Dsp();
	~Dsp();
};



Dsp::Dsp() {
}

Dsp::~Dsp() {
}

inline void Dsp::init(uint32_t sample_rate)
{
	fSampleRate = sample_rate;
}

void Dsp::compute(int count, FAUSTFLOAT *input0, FAUSTFLOAT *input1, FAUSTFLOAT *output0)
{
#define fVslider0 (*fVslider0_)
	float fSlow0 = 0.01f * float(fVslider0);
	float fSlow1 = 1.0f - fSlow0;
	for (int i0 = 0; i0 < count; i0 = i0 + 1) {
		output0[i0] = FAUSTFLOAT(fSlow1 * float(input0[i0]) + fSlow0 * float(input1[i0]));
	}
#undef fVslider0
}


void Dsp::connect(uint32_t port,void* data)
{
	switch ((PortIndex)port)
	{
	case 4: 
		fVslider0_ = static_cast<float*>(data); // , 5e+01f, 0.0f, 1e+02f, 1.0f 
		break;
	default:
		break;
	}
}

Dsp *plugin() {
	return new Dsp();
}

void Dsp::del_instance(Dsp *p)
{
	delete p;
}
} // end namespace wet_dry
