#include "shm_connection.h"

#include <boost/lexical_cast.hpp>

#include "liblog.h"
#include "libpar.h"
#include "ipcdefines.h"
#include "conversion.h"
#include "szbdefines.h"

volatile sig_atomic_t g_signals_blocked = 0;
volatile sig_atomic_t g_signum_received = 0;

const void* SHMAT_ERROR = (void*)-1;

RETSIGTYPE g_SignalHandler(int signum)
{
	if (g_signals_blocked) {
		g_signum_received = signum;
	} else {
		signal(signum, SIG_DFL);
		raise(signum);
	}
}

void lock_signals()
{
	g_signals_blocked = 1;
}

void unlock_signals()
{
	if (g_signum_received != 0) {
		int signum = g_signum_received;
		g_signum_received = 0;
		g_signals_blocked = 0;
		raise(signum);
	} else {
		g_signals_blocked = 0;
	}
}

ShmConnection::ShmConnection(): 
_registered(false), 
_configured(false), 
_connected(false),
_szarp_config(new TSzarpConfig()),
_shm_desc(0), 
_sem_desc(0), 
_values_count(0)
{
}

void ShmConnection::register_signals()
{
	int ret = 0;
	struct sigaction sa{};
	sigset_t block_mask{};

	sigfillset(&block_mask);
	sa.sa_mask = block_mask;

	// deliver following signals using custom signal handler
	// to avoid leaving attached shared memory
	sa.sa_handler = g_SignalHandler;
	sa.sa_flags = SA_RESTART;
	ret = sigaction(SIGTERM, &sa, NULL);
	assert (ret == 0);
	ret = sigaction(SIGINT, &sa, NULL);
	assert (ret == 0);
	ret = sigaction(SIGHUP, &sa, NULL);
	assert (ret == 0);
	
	_registered = true;
}

bool ShmConnection::configure()
{
	std::wstring ipk_path;	

	libpar_init_with_filename("/etc/" PACKAGE_NAME "/" PACKAGE_NAME ".cfg", 1);

	char *config_param = libpar_getpar("global", "IPK", 0);
	if (config_param == nullptr) {
		sz_log(1, "ShmConnection::configure(): set 'IPK' param in szarp.cfg file");
		libpar_done();
		return false;
	} else {
		ipk_path = SC::L2S(config_param);
		free(config_param);
	}

	config_param = libpar_getpar("global", "parcook_path", 0);
	if (config_param == nullptr) {
		sz_log(1, "ShmConnection::configure(): set 'parcook_path' param in szarp.cfg file");
		libpar_done();
		return false;
	} else {
		_parcook_path = std::string(config_param);
		free(config_param);
	}

	config_param = libpar_getpar("", "probes_buffer_size", 0);
	if (config_param != nullptr) {
		_values_count = boost::lexical_cast<int>(config_param);
		free(config_param);
		sz_log(9, "ShmConnection::configure(): buffer size %d detected in szarp.cfg file", _values_count);
	}
	
	libpar_done();

	xmlInitParser();

	if (_szarp_config->loadXML(ipk_path) != 0) {
		sz_log(1, "ShmConnection::configure(): error loading configuration from file %s",
				SC::S2A(ipk_path).c_str());
		xmlCleanupParser();
		return false;
	}
	sz_log(5, "ShmConnection::configure(): IPK file %s successfully loaded", SC::S2A(ipk_path).c_str());
	
	_params_count = _szarp_config->GetParamsCount() + _szarp_config->GetDefinedCount();

	xmlCleanupParser();
	
	_configured = true;
	return true;
}

bool ShmConnection::connect()
{
	key_t shm_key;
	key_t sem_key;
	const int max_attempts_no = 60;

	assert(_values_count >= 0);
	assert(_params_count >= 0);
	_shm_segment.resize(_values_count * _params_count + SHM_PROBES_BUF_DATA_OFF);

	shm_key = ftok(_parcook_path.c_str(), SHM_PROBES_BUF);
	if (shm_key == -1) {
		sz_log(1, "ShmConnection::connect(): ftok() for shm failed, errno %d, path '%s'",
			errno, _parcook_path.c_str());
		return false;
	}

	sem_key = ftok(_parcook_path.c_str(), SEM_PARCOOK);
	if (sem_key == -1) {
		sz_log(1, "ShmConnection::connect(): ftok() for sem failed, errno %d, path '%s'",
				errno, _parcook_path.c_str());
		return false;
	}

	_shm_desc = shmget(shm_key, 1, 00600);
	_sem_desc = semget(sem_key, 2, 00600);
	for (int i = 0; ((_shm_desc == -1) || (_sem_desc == -1)) && (i < max_attempts_no-1); ++i)
	{
		sleep(1);
		_shm_desc = shmget(shm_key, 1, 00600);
		_sem_desc = semget(sem_key, 2, 00600);
	}

	if (_shm_desc == -1) {
		sz_log(1, "ShmConnection::connect(): error getting shm id, errno %d, key %d", 
				errno, shm_key);
		return false;
	}
	if (_sem_desc == -1) {
		sz_log(1, "ShmConnection::connect(): error getting semid, errno %d, key %d",
				errno, sem_key);
		return false;
	}
	_connected = true;
	return true;
}

bool ShmConnection::shm_open(struct sembuf* semaphores)
{
	semaphores[0].sem_num = SEM_PROBES_BUF + 1;
	semaphores[0].sem_op = 0;
	semaphores[0].sem_flg = SEM_UNDO;
	semaphores[1].sem_num = SEM_PROBES_BUF;
	semaphores[1].sem_op = 1;
	semaphores[1].sem_flg = SEM_UNDO;

	if (semop(_sem_desc, semaphores, 2) == -1) {
		sz_log(1, "ShmConnection:shm_open(): cannot open semaphore, errno %d", errno);
		return false;
	}
	return true;
}

void ShmConnection::shm_close(struct sembuf* semaphores)
{
	semaphores[0].sem_num = SEM_PROBES_BUF;
	semaphores[0].sem_op = -1;
	semaphores[0].sem_flg = SEM_UNDO;
	if (semop(_sem_desc, semaphores, 1) == -1) {
		sz_log(1, "ShmConnection:shm_close: cannot release semaphore, errno %d", errno);
	}
}

int16_t* ShmConnection::attach() 
{
	int16_t* segment = nullptr;
	do { 
		segment = (int16_t*)shmat(_shm_desc, nullptr, SHM_RDONLY); 

	} while((segment == SHMAT_ERROR) && errno == EINTR);

	if (segment == SHMAT_ERROR) {
		sz_log(1, "ShmConnection:update_segment(): cannot attach segment, errno %d", errno);
	}

	return segment;
}

void ShmConnection::detach(int16_t** segment) 
{
	if (shmdt(*segment) == -1) {
		sz_log(1, "ShmConnection::update_segment(): cannot detach segment, errno %d", errno);
	}
}

void ShmConnection::update_segment()
{
	bool success = false;
	struct sembuf semaphores[2];

	lock_signals();
	if (shm_open(semaphores)) {
		int16_t* attached_segment = attach();
		if (attached_segment != SHMAT_ERROR) {
			assert(_shm_segment.size() >= 0);
			std::copy(attached_segment, attached_segment + _shm_segment.size(), _shm_segment.begin());
			detach(&attached_segment);
			success = true;
		}
		shm_close(semaphores);
	}
	unlock_signals();

	if (!success) {
		_shm_segment.clear();
		throw ShmError("ShmConnection: couldn't attach shared memory");
	}
}

int ShmConnection::param_index_from_path(std::string path)
{
	std::string name_path;
	std::wstring w_name_path;
	
	/* Remove "/.szc" */
	path = path.substr(0, path.rfind("/"));
	/* Get last 3 parts of szbase name */
	for (int name_part = 0; name_part < 3; ++name_part) {
		name_path = path.substr(path.rfind("/"), std::string::npos) + name_path; 
		path = path.substr(0, path.rfind("/"));
	}
	/* Remove first slash */
	name_path.erase(name_path.begin());
	
	w_name_path.assign(name_path.begin(), name_path.end());

	TParam* param = _szarp_config->GetFirstParam();
	while (param != nullptr) {
		std::wstring szbase_name = param->GetSzbaseName();
		if (szbase_name.compare(w_name_path) == 0)
			return param->GetIpcInd();		
		param = param->GetNextGlobal();
	}

	sz_log(1, "ShmConnection::param_index_from_path: param not found!");
	return -1;
}

int ShmConnection::get_count() 
{
	if (_shm_segment.empty()) return -1;

	return _shm_segment[SHM_PROBES_BUF_CNT_INDEX]; 			
}
	
int ShmConnection::get_pos() 
{
	if (_shm_segment.empty()) return -1;

	return _shm_segment[SHM_PROBES_BUF_POS_INDEX];
}

std::vector<int16_t> ShmConnection::get_values(int param_index)
{
	std::vector<int16_t> param_values(_values_count, SZB_FILE_NODATA);

	int16_t count = _shm_segment[SHM_PROBES_BUF_CNT_INDEX]; 				
	int16_t pos = _shm_segment[SHM_PROBES_BUF_POS_INDEX]; 				
				
	auto data = _shm_segment.begin() + SHM_PROBES_BUF_DATA_OFF;

	int param_off = _values_count * param_index;
	int param_end_off = param_off + _values_count - 1;
	int pos_abs = pos + param_off;

	if (count < _values_count) {
		std::copy(data + param_off, data + param_off + pos,  param_values.begin());
		return param_values;
	} else {
		int old_vals_cnt = param_end_off - pos_abs + 1;
		std::copy(data + pos_abs, data + pos_abs + old_vals_cnt, param_values.begin());
		if (pos == 0) return param_values;
		std::copy(data + param_off, data + param_off + pos, param_values.begin() + old_vals_cnt);
		//std::copy(data + param_off, data + param_off + _values_count,  param_values.begin());
		return param_values;
	}
}
