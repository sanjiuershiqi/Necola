#include "Vars.h"


void CGlobal_Vars::Load()
{
	inipp::Ini<char> ini;
	std::ifstream is("kpatch.ini");
	ini.parse(is);
	{
		// ADS configuration block
		inipp::extract(ini.sections["AdsSupport"]["enableAdsSupport"], enableAdsSupport);
	}
}
