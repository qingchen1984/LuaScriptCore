//
// Created by 冯鸿杰 on 16/9/27.
//

#include <limits>
#include <stdio.h>
#include <ctype.h>
#include "LuaObjectClass.h"
#include "LuaObjectInstanceDescriptor.h"
#include "LuaContext.h"
#include "LuaValue.h"
#include "LuaTuple.h"
#include "LuaSession.h"

using namespace cn::vimfung::luascriptcore;
using namespace cn::vimfung::luascriptcore::modules::oo;

/**
 *  对象销毁处理
 *
 *  @param state 状态机
 *
 *  @return 参数数量
 */
static int objectDestroyHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    LuaArgumentList args;
    session -> parseArguments(args);

    if (args.size() > 0 && args[0] -> getType() == LuaValueTypeObject)
    {
        LuaObjectDescriptor *objDesc = args[0] -> toObject();

        if (objectClass -> getObjectDestroyHandler() != NULL)
        {
            objectClass -> getObjectDestroyHandler() (objDesc);
        }

        //调用实例对象的destroy方法
        lua_pushvalue(state, 1);

        lua_getfield(state, -1, "destroy");
        if (lua_isfunction(state, -1))
        {
            lua_pushvalue(state, 1);
            lua_pcall(state, 1, 0, 0);
        }
        else
        {
            lua_pop(state, 1);
        }

        lua_pop(state, 1);
    }

    for (LuaArgumentList::iterator it = args.begin(); it != args.end() ; ++it)
    {
        LuaValue *value = *it;
        value -> release();
    }

    objectClass -> getContext() -> destorySession(session);

    return 0;
}

/**
 *  对象转换为字符串处理
 *
 *  @param state 状态机
 *
 *  @return 参数数量
 */
static int objectToStringHandler (lua_State *state)
{
    std::string desc;

    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    if (lua_gettop(state) > 0)
    {
        int type = lua_type(state, 1);

        //由于加入了实例的super对象，因此需要根据不同类型进行不同输出。since ver 1.3
        switch (type)
        {
            case LUA_TUSERDATA:
            {
                LuaArgumentList args;
                session -> parseArguments(args);

                LuaObjectDescriptor *instance = args[0] -> toObject();

                if (objectClass -> getObjectDescriptionHandler() != NULL)
                {
                    desc = objectClass -> getObjectDescriptionHandler()(instance);
                }

                if (desc.empty())
                {
                    char strbuf[256] = {0};
                    int size = sprintf(strbuf, "[%s object]", objectClass -> getName().c_str());
                    strbuf[size] = '\0';

                    lua_pushstring(state, strbuf);
                }
                else
                {
                    lua_pushstring(state, desc.c_str());
                }
                break;
            }
            case LUA_TTABLE:
            {
                lua_pushstring(state, "<SuperClass Type>");
                break;
            }
            default:
            {
                lua_pushstring(state, "<Unknown Type>");
                break;
            }
        }
    }
    else
    {
        lua_pushstring(state, "<Unknown Type>");
    }

    objectClass -> getContext() -> destorySession(session);

    return 1;
}

/**
 *  创建对象时处理
 *
 *  @param state 状态机
 *
 *  @return 参数数量
 */
static int objectCreateHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(2));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    if (objectClass -> getObjectCreatedHandler() != NULL)
    {
        objectClass -> getObjectCreatedHandler() (objectClass);
    }

    //调用实例对象的init方法
    lua_getfield(state, -1, "init");
    if (lua_isfunction(state, -1))
    {
        lua_pushvalue(state, -2);

        //将create传入的参数传递给init方法
        //-3 代表有3个非参数值在栈中，由栈顶开始计算，分别是：实例对象，init方法，实例对象
        int paramCount = lua_gettop(state) - 3;
        for (int i = 1; i <= paramCount; i++)
        {
            lua_pushvalue(state, i);
        }

        lua_pcall(state, paramCount + 1, 0, 0);
    }
    else
    {
        lua_pop(state, 1);
    }

    objectClass -> getContext() -> destorySession(session);

    return 1;
}

/**
 实例对象更新索引处理

 @param state 状态机
 @return 参数数量
 */
static int instanceNewIndexHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    //先找到实例对象的元表，向元表添加属性
    lua_getmetatable(state, 1);
    if (lua_istable(state, -1))
    {
        lua_pushvalue(state, 2);
        lua_pushvalue(state, 3);
        lua_rawset(state, -3);
    }

    objectClass -> getContext() -> destorySession(session);

    return 0;
}

/**
 *  子类化
 *
 *  @param state 状态机
 *
 *  @return 参数数量
 */
static int subClassHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(2));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    if (lua_gettop(state) > 0)
    {
        const char *subclassName = luaL_checkstring(state, 1);

        if (objectClass -> getSubClassHandler() != NULL)
        {
            objectClass -> getSubClassHandler()(objectClass, subclassName);
        }
    }

    objectClass -> getContext() -> destorySession(session);

    return 0;
}

/**
 判断是否是该类型的子类

 @param state 状态机
 @return 参数数量
 */
static int subclassOfHandler (lua_State *state)
{
    if (lua_gettop(state) == 0)
    {
        lua_pushboolean(state, false);
        return 1;
    }

    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(2));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    if (lua_type(state, 1) == LUA_TTABLE)
    {
        lua_getfield(state, 1, "_nativeClass");
        if (lua_type(state, -1) == LUA_TLIGHTUSERDATA)
        {
            LuaObjectClass *checkClass = (LuaObjectClass *)lua_touserdata(state, -1);

            bool flag = objectClass -> subclassOf(checkClass);
            lua_pushboolean(state, flag);
        }
        else
        {
            lua_pushboolean(state, false);
        }
    }
    else
    {
        lua_pushboolean(state, false);
    }

    objectClass -> getContext() -> destorySession(session);

    return 1;
}

/**
 判断是否是该类型的实例对象

 @param state 状态机
 @return 参数数量
 */
static int instanceOfHandler (lua_State *state)
{
    if (lua_gettop(state) < 2)
    {
        lua_pushboolean(state, false);
        return 1;
    }

    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(2));
    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    //表示有实例对象传入
    LuaUserdataRef ref = (LuaUserdataRef)lua_touserdata(state, 1);
    LuaObjectInstanceDescriptor *objDesc = (LuaObjectInstanceDescriptor *)ref -> value;

    if (lua_type(state, 2) == LUA_TTABLE)
    {
        lua_getfield(state, 2, "_nativeClass");
        if (lua_type(state, -1) == LUA_TLIGHTUSERDATA)
        {
            LuaObjectClass *objectClass = (LuaObjectClass *)lua_topointer(state, -1);

            bool flag = objDesc -> instanceOf(objectClass);
            lua_pushboolean(state, flag);
        }
        else
        {
            lua_pushboolean(state, false);
        }
    }
    else
    {
        lua_pushboolean(state, false);
    }

    objectClass -> getContext() -> destorySession(session);

    return 1;
}

/**
 * 实例方法路由处理
 *
 * @param state lua状态机
 *
 * @return 返回值数量
 */
static int instanceMethodRouteHandler(lua_State *state)
{
    int returnCount = 0;

    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    std::string methodName = lua_tostring(state, lua_upvalueindex(2));

    if (lua_type(state, 1) != LUA_TUSERDATA)
    {
        std::string errMsg = "call " + methodName + " method error : missing self parameter, please call by instance:methodName(param)";
        objectClass -> getContext() -> raiseException(errMsg);

        //回收内存
        lua_gc(state, LUA_GCCOLLECT, 0);

        return 0;
    }

    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    LuaInstanceMethodHandler handler = objectClass -> getInstanceMethodHandler(methodName);
    if (handler != NULL)
    {
        LuaArgumentList args;
        session -> parseArguments(args);
        LuaObjectDescriptor *instance = args[0] -> toObject();

        LuaArgumentList methodArgs;
        for (int i = 1; i < args.size(); i++)
        {
            LuaValue *value = args[i];
            methodArgs.push_back(value);
        }

        cn::vimfung::luascriptcore::LuaValue *retValue = handler (instance, objectClass, methodName, methodArgs);

        if (retValue != NULL)
        {
            returnCount = session -> setReturnValue(retValue);
            //释放返回值
            retValue -> release();
        }

        //释放参数内存
        for (LuaArgumentList::iterator it = args.begin(); it != args.end() ; ++it)
        {
            LuaValue *item = *it;
            item -> release();
        }
    }

    objectClass -> getContext() -> destorySession(session);

    return returnCount;
}

/**
 * 实例设置器路由处理
 *
 * @param state lua状态机
 *
 * @return 返回值数量
 */
static int instanceSetterRouteHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    std::string fieldName = lua_tostring(state, lua_upvalueindex(2));

    if (lua_type(state, 1) != LUA_TUSERDATA)
    {
        std::string errMsg = "call " + fieldName + " method error : missing self parameter, please call by instance:methodName(param)";
        objectClass -> getContext() -> raiseException(errMsg);

        //回收内存
        lua_gc(state, LUA_GCCOLLECT, 0);

        return 0;
    }

    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    LuaInstanceSetterHandler handler = objectClass -> getInstanceSetterHandler(fieldName);
    if (handler != NULL)
    {
        LuaArgumentList args;
        session -> parseArguments(args);
        LuaObjectDescriptor *instance = args[0] -> toObject();

        LuaValue *value = NULL;
        if (args.size() > 1)
        {
            value = args[1];
        }
        else
        {
            value = LuaValue::NilValue();
        }

        handler (instance, objectClass, fieldName, value);

        //释放参数内存
        for (LuaArgumentList::iterator it = args.begin(); it != args.end() ; ++it)
        {
            LuaValue *item = *it;
            item -> release();
        }
    }

    objectClass -> getContext() -> destorySession(session);

    return 0;
}

/**
 * 实例获取器路由处理
 *
 * @param state lua状态机
 *
 * @return 返回值数量
 */
static int instanceGetterRouteHandler (lua_State *state)
{
    LuaObjectClass *objectClass = (LuaObjectClass *)lua_touserdata(state, lua_upvalueindex(1));
    std::string fieldName = lua_tostring(state, lua_upvalueindex(2));

    if (lua_type(state, 1) != LUA_TUSERDATA)
    {
        std::string errMsg = "call " + fieldName + " method error : missing self parameter, please call by instance:methodName(param)";
        objectClass -> getContext() -> raiseException(errMsg);

        //回收内存
        lua_gc(state, LUA_GCCOLLECT, 0);

        return 0;
    }

    LuaSession *session = objectClass -> getContext() -> makeSession(state);

    LuaInstanceGetterHandler handler = objectClass -> getGetterHandler(fieldName);
    if (handler != NULL)
    {
        LuaArgumentList args;
        session -> parseArguments(args);
        LuaObjectDescriptor *instance = args[0] -> toObject();

        LuaValue *retValue = handler (instance, objectClass, fieldName);

        session -> setReturnValue(retValue);

        if (retValue != NULL)
        {
            //释放返回值
            retValue -> release();
        }

        //释放参数内存
        for (LuaArgumentList::iterator it = args.begin(); it != args.end() ; ++it)
        {
            LuaValue *item = *it;
            item -> release();
        }
    }

    objectClass -> getContext() -> destorySession(session);

    return 1;
}

LuaObjectClass::LuaObjectClass(LuaObjectClass *superClass)
{
    _superClass = superClass;
    _classObjectCreatedHandler = NULL;
    _classObjectDescriptionHandler = NULL;
    _classObjectDestroyHandler = NULL;
    _subclassHandler = NULL;
}

void LuaObjectClass::onObjectCreated(LuaClassObjectCreatedHandler handler)
{
    _classObjectCreatedHandler = handler;
}

void LuaObjectClass::onObjectDestroy(LuaClassObjectDestroyHandler handler)
{
    _classObjectDestroyHandler = handler;
}

void LuaObjectClass::onObjectGetDescription (LuaClassObjectGetDescriptionHandler handler)
{
    _classObjectDescriptionHandler = handler;
}

void LuaObjectClass::onSubClass (LuaSubClassHandler handler)
{
    _subclassHandler = handler;
}

void LuaObjectClass::onRegister(const std::string &name, LuaContext *context)
{

    LuaModule::onRegister(name, context);

    lua_State *state = context -> getMainSession() -> getState();
    lua_getglobal(state, name.c_str());

    if (lua_istable(state, -1))
    {
        //关联本地类型
        lua_pushlightuserdata(state, this);
        lua_setfield(state, -2, "_nativeClass");

        //设置类型名称。since ver 1.3
        lua_pushstring(state, name.c_str());
        lua_setfield(state, -2, "name");

        //关联索引
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");

        //创建方法
        lua_pushlightuserdata(state, context);
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, objectCreateHandler, 2);
        lua_setfield(state, -2, "create");

        //子类化对象方法
        lua_pushlightuserdata(state, context);
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, subClassHandler, 2);
        lua_setfield(state, -2, "subclass");

        //增加子类判断方法, since ver 1.3
        lua_pushlightuserdata(state, context);
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, subclassOfHandler, 2);
        lua_setfield(state, -2, "subclassOf");

        if (_superClass != NULL)
        {
            //存在父类，则直接设置父类为元表
            std::string superClassName = _superClass -> getName();
            lua_getglobal(state, superClassName.c_str());
            if (lua_istable(state, -1))
            {
                //关联父类
                lua_pushvalue(state, -1);
                lua_setfield(state, -3, "super");

                //设置父类元表
                lua_setmetatable(state, -2);
            }
            else
            {
                lua_pop(state, 1);
            }
        }

        //创建类实例元表
        std::string metaName = _getMetaClassName(name);
        luaL_newmetatable(state, metaName.c_str());

        lua_pushlightuserdata(state, this);
        lua_setfield(state, -2, "_nativeClass");

        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");

        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, objectDestroyHandler, 1);
        lua_setfield(state, -2, "__gc");

        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, objectToStringHandler, 1);
        lua_setfield(state, -2, "__tostring");

        //给类元表绑定该实例元表
        lua_getglobal(state, name.c_str());
        lua_pushvalue(state, -2);
        lua_setfield(state, -2, "prototype");
        lua_pop(state, 1);

        if (_superClass != NULL)
        {
            //获取父级元表
            std::string superClassMetaName = _getMetaClassName(_superClass -> getName());
            luaL_getmetatable(state, superClassMetaName.c_str());
            if (lua_istable(state, -1))
            {
                //设置父类访问属性 since ver 1.3
                lua_pushvalue(state, -1);
                lua_setfield(state, -3, "super");

                //设置父类元表
                lua_setmetatable(state, -2);
            }
            else
            {
                lua_pop(state, 1);
            }
        }
        else
        {
            //Object类需要增加一些特殊方法
            //创建instanceOf方法 since ver 1.3
            lua_pushlightuserdata(state, context);
            lua_pushlightuserdata(state, this);
            lua_pushcclosure(state, instanceOfHandler, 2);
            lua_setfield(state, -2, "instanceOf");
        }
    }

    lua_pop(state, 1);
}

LuaClassObjectCreatedHandler LuaObjectClass::getObjectCreatedHandler()
{
    return _classObjectCreatedHandler;
}

LuaClassObjectDestroyHandler LuaObjectClass::getObjectDestroyHandler()
{
    return  _classObjectDestroyHandler;
}

LuaClassObjectGetDescriptionHandler  LuaObjectClass::getObjectDescriptionHandler()
{
    return  _classObjectDescriptionHandler;
}

LuaSubClassHandler LuaObjectClass::getSubClassHandler()
{
    return _subclassHandler;
}

void LuaObjectClass::registerInstanceField(std::string fieldName, LuaInstanceGetterHandler getterHandler, LuaInstanceSetterHandler setterHandler)
{
    //与iOS中的属性getter和setter方法保持一致, getter直接是属性名称,setter则需要将属性首字母大写并在前面加上set
    char upperCStr[2] = {0};
    upperCStr[0] = (char)toupper(fieldName[0]);
    std::string upperStr = upperCStr;
    std::string fieldNameStr = fieldName.c_str() + 1;
    std::string setterMethodName = "set" + upperStr + fieldNameStr;

    lua_State *state = getContext() -> getMainSession() -> getState();
    std::string metaClassName = _getMetaClassName(getName());
    luaL_getmetatable(state, metaClassName.c_str());
    if (lua_istable(state, -1))
    {
        //设置Setter方法
        if (setterHandler != NULL)
        {
            lua_pushlightuserdata(state, this);
            lua_pushstring(state, fieldName.c_str());
            lua_pushcclosure(state, instanceSetterRouteHandler, 2);

            lua_setfield(state, -2, setterMethodName.c_str());

            _instanceSetterMap[fieldName] = setterHandler;
        }

        //注册Getter方法
        if (getterHandler != NULL)
        {
            lua_pushlightuserdata(state, this);
            lua_pushstring(state, fieldName.c_str());
            lua_pushcclosure(state, instanceGetterRouteHandler, 2);
            
            lua_setfield(state, -2, fieldName.c_str());
            
            _instanceGetterMap[fieldName] = getterHandler;
        }
    }

    lua_pop(state, 1);
}

LuaObjectClass* LuaObjectClass::getSupuerClass()
{
    return _superClass;
}

bool LuaObjectClass::subclassOf(LuaObjectClass *type)
{
    bool isSubclass = false;

    LuaObjectClass *tmpClass = this;
    while (tmpClass != NULL)
    {
        if (tmpClass -> getName() == type -> getName())
        {
            isSubclass = true;
            break;
        }

        tmpClass = tmpClass -> getSupuerClass();
    }

    return isSubclass;
}

void LuaObjectClass::registerMethod(
        std::string methodName,
        LuaModuleMethodHandler handler)
{
    LuaModule::registerMethod(methodName, handler);
}

void LuaObjectClass::registerInstanceMethod(
        std::string methodName,
        LuaInstanceMethodHandler handler)
{
    lua_State *state = getContext() -> getMainSession() -> getState();
    std::string metaClassName = _getMetaClassName(getName());
    luaL_getmetatable(state, metaClassName.c_str());
    if (lua_istable(state, -1))
    {
        lua_pushlightuserdata(state, this);
        lua_pushstring(state, methodName.c_str());
        lua_pushcclosure(state, instanceMethodRouteHandler, 2);
        lua_setfield(state, -2, methodName.c_str());

        _instanceMethodMap[methodName] = handler;
    }

    lua_pop(state, 1);
}

LuaInstanceMethodHandler LuaObjectClass::getInstanceMethodHandler(std::string methodName)
{
    LuaInstanceMethodMap::iterator it =  _instanceMethodMap.find(methodName.c_str());
    if (it != _instanceMethodMap.end())
    {
        return it -> second;
    }

    return NULL;
}

LuaInstanceSetterHandler LuaObjectClass::getInstanceSetterHandler(std::string fieldName)
{
    LuaInstanceSetterMap::iterator it =  _instanceSetterMap.find(fieldName.c_str());
    if (it != _instanceSetterMap.end())
    {
        return it -> second;
    }

    return NULL;
}

LuaInstanceGetterHandler LuaObjectClass::getGetterHandler(std::string fieldName)
{
    LuaInstanceGetterMap::iterator it =  _instanceGetterMap.find(fieldName.c_str());
    if (it != _instanceGetterMap.end())
    {
        return it -> second;
    }

    return NULL;
}

void LuaObjectClass::createLuaInstance(LuaObjectInstanceDescriptor *objectDescriptor)
{
    lua_State *state = getContext() -> getCurrentSession() -> getState();

    //先为实例对象在lua中创建内存
    LuaUserdataRef ref = (LuaUserdataRef)lua_newuserdata(state, sizeof(LuaUserdataRef));
    //创建本地实例对象，赋予lua的内存块并进行保留引用
    ref -> value = objectDescriptor;

    //对描述器进行引用
    objectDescriptor -> retain();

    //创建一个临时table作为元表，用于在lua上动态添加属性或方法
    lua_newtable(state);

    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");

    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, instanceNewIndexHandler, 1);
    lua_setfield(state, -2, "__newindex");

    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, objectDestroyHandler, 1);
    lua_setfield(state, -2, "__gc");

    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, objectToStringHandler, 1);
    lua_setfield(state, -2, "__tostring");

    lua_pushvalue(state, -1);
    lua_setmetatable(state, -3);

    std::string metaClassName = _getMetaClassName(getName());
    luaL_getmetatable(state, metaClassName.c_str());
    if (lua_istable(state, -1))
    {
        lua_setmetatable(state, -2);
    }
    else
    {
        lua_pop(state, 1);
    }

    lua_pop(state, 1);
}

void LuaObjectClass::push(LuaObjectInstanceDescriptor *objectDescriptor)
{
    //对LuaObjectClass的类型对象需要进行特殊处理
    lua_State *state = getContext() -> getCurrentSession() -> getState();

    createLuaInstance(objectDescriptor);

    //调用默认init
    lua_getfield(state, -1, "init");
    if (lua_isfunction(state, -1))
    {
        lua_pushvalue(state, -2);
        lua_pcall(state, 1, 0, 0);
    }
    else
    {
        lua_pop(state, 1);
    }
}

std::string LuaObjectClass::_getMetaClassName(std::string className)
{
    return "_" + className + "_META_";
}
