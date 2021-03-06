#ifndef SBK_PAGEPARAMWRAPPER_H
#define SBK_PAGEPARAMWRAPPER_H

#include <shiboken.h>

#include <PyParameter.h>

NATRON_NAMESPACE_ENTER; NATRON_PYTHON_NAMESPACE_ENTER;
class PageParamWrapper : public PageParam
{
public:
    inline void _addAsDependencyOf_protected(int fromExprDimension, Param * param, int thisDimension) { PageParam::_addAsDependencyOf(fromExprDimension, param, thisDimension); }
    virtual ~PageParamWrapper();
    static void pysideInitQtMetaTypes();
};
NATRON_PYTHON_NAMESPACE_EXIT; NATRON_NAMESPACE_EXIT;

#endif // SBK_PAGEPARAMWRAPPER_H

