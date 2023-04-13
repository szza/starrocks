// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "exec/pipeline/aggregate/spillable_aggregate_blocking_source_operator.h"

#include <algorithm>

#include "common/status.h"
#include "exec/pipeline/aggregate/aggregate_blocking_source_operator.h"

namespace starrocks::pipeline {
void SpillableAggregateBlockingSourceOperator::close(RuntimeState* state) {
    AggregateBlockingSourceOperator::close(state);
}

bool SpillableAggregateBlockingSourceOperator::has_output() const {
    if (AggregateBlockingSourceOperator::has_output()) {
        return true;
    }

    if (!_aggregator->spiller()->spilled()) {
        return false;
    }
    // has output data from spiller.
    if (_aggregator->spiller()->has_output_data()) {
        return true;
    }
    // has eos chunk
    if (_aggregator->is_spilled_eos() && _has_last_chunk) {
        return true;
    }
    return false;
}

bool SpillableAggregateBlockingSourceOperator::is_finished() const {
    if (_is_finished) {
        return true;
    }
    if (!_aggregator->spiller()->spilled()) {
        return AggregateBlockingSourceOperator::is_finished();
    }
    return AggregateBlockingSourceOperator::is_finished() && _aggregator->is_spilled_eos() && !_has_last_chunk;
}

Status SpillableAggregateBlockingSourceOperator::set_finished(RuntimeState* state) {
    _is_finished = true;
    RETURN_IF_ERROR(AggregateBlockingSourceOperator::set_finished(state));
    _aggregator->spiller()->set_finished();
    return Status::OK();
}

StatusOr<ChunkPtr> SpillableAggregateBlockingSourceOperator::pull_chunk(RuntimeState* state) {
    if (!_aggregator->spiller()->spilled()) {
        return AggregateBlockingSourceOperator::pull_chunk(state);
    }

    ASSIGN_OR_RETURN(auto res, _pull_spilled_chunk(state));

    if (res != nullptr) {
        const int64_t old_size = res->num_rows();
        RETURN_IF_ERROR(eval_conjuncts_and_in_filters(_aggregator->conjunct_ctxs(), res.get()));
        _aggregator->update_num_rows_returned(-(old_size - static_cast<int64_t>(res->num_rows())));
    }

    return res;
}

StatusOr<ChunkPtr> SpillableAggregateBlockingSourceOperator::_pull_spilled_chunk(RuntimeState* state) {
    DCHECK(_accumulator.need_input());
    ChunkPtr res;
    if (!_aggregator->is_spilled_eos()) {
        auto executor = _aggregator->spill_channel()->io_executor();
        ASSIGN_OR_RETURN(auto chunk,
                         _aggregator->spiller()->restore(state, *executor, spill::MemTrackerGuard(tls_mem_tracker)));

        if (chunk->is_empty()) {
            return chunk;
        }

        RETURN_IF_ERROR(_aggregator->evaluate_groupby_exprs(chunk.get()));
        RETURN_IF_ERROR(_aggregator->evaluate_agg_fn_exprs(chunk.get(), true));
        ASSIGN_OR_RETURN(res, _aggregator->streaming_compute_agg_state(chunk->num_rows(), false));
        _accumulator.push(std::move(res));

    } else {
        _has_last_chunk = false;
        ASSIGN_OR_RETURN(res, _aggregator->pull_eos_chunk());
        _accumulator.push(std::move(res));
        _accumulator.finalize();
    }

    if (_accumulator.has_output()) {
        auto accumulated = std::move(_accumulator.pull());
        return accumulated;
    }

    return nullptr;
}

} // namespace starrocks::pipeline