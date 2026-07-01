#include "items/GradeRegistry.h"

float GradeRegistry::GetStatCap(ModuleGrade grade, bool isMerged) {
    (void)grade;            // grade sets absolute stat range, not the percentage ceiling
    return isMerged ? kMergedCap : 1.0f;
}

bool GradeRegistry::AllowsPrimaryGraft(ModuleGrade grade) {
    return grade != ModuleGrade::Mythic;
}

void GradeRegistry::Apply(Item& item) {
    item.baseStatCap = GetStatCap(item.grade, item.isMerged);
}
