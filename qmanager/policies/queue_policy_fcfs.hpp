/*****************************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, LICENSE)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\*****************************************************************************/

#ifndef QUEUE_POLICY_FCFS_HPP
#define QUEUE_POLICY_FCFS_HPP

#include <jansson.h>
#include "qmanager/policies/base/queue_policy_base.hpp"

namespace Flux {
namespace queue_manager {
namespace detail {

template<class reapi_type>
class queue_policy_fcfs_t : public queue_policy_base_t
{
public:
    virtual ~queue_policy_fcfs_t ();
    virtual int run_sched_loop (void *h, bool use_alloced_queue);
    virtual int reconstruct_resource (void *h, std::shared_ptr<job_t> job,
                                      std::string &R_out);
    virtual int apply_params ();
    virtual int handle_match_success (flux_jobid_t jobid, const char *status,
                                      const char *R, int64_t at, double ov);
    virtual int handle_match_failure (flux_jobid_t jobid, int errcode);
    int cancel (void *h, flux_jobid_t id, const char *R, bool noent_ok,
                bool &full_removal) override;

private:
    int pack_jobs (json_t *jobs);
    int allocate_jobs (void *h, bool use_alloced_queue);
    bool m_queue_depth_limit = false;
    job_map_iter m_iter;
};

} // namespace Flux::queue_manager::detail
} // namespace Flux::queue_manager
} // namespace Flux

#endif // QUEUE_POLICY_FCFS_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
