#ifndef __SERVER_CMD_VALUE_H__
#define __SERVER_CMD_VALUE_H__

#include <boost/format.hpp>

#include "data/param.h"
#include "data/probe_type.h"

#include "locations/command.h"

class ValueSnd : public Command {
public:
	ValueSnd( std::shared_ptr<const Param> p , ProbeType pt ) : p(p) , pt(pt) {}
	
	virtual ~ValueSnd() {}

	virtual to_send send_str()
	{
		return to_send( str(
				boost::format("\"%s\" %d %s") %
					p->get_name() %
					p->get_value( pt ) %
					(pt == ProbeType() ? "" : pt.to_string()) ));
	}

	virtual bool single_shot()
	{	return true; }

protected:
	std::shared_ptr<const Param> p;
	ProbeType pt;
};

#endif /* end of include guard: __SERVER_CMD_VALUE_H__ */

