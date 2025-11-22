extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/idset.h>
}

#include <map>
#include <unordered_set>
#include <unistd.h>
#include <regex>
#include <jansson.h>
#include "resource/readers/resource_reader_jgf_shorthand.hpp"
#include "resource/store/resource_graph_store.hpp"
#include "resource/planner/c/planner.h"

using namespace Flux;
using namespace Flux::resource_model;

resource_reader_jgf_shorthand_t::~resource_reader_jgf_shorthand_t ()
{
}

int resource_reader_jgf_shorthand_t::fetch_additional_vertices (
    resource_graph_t &g,
    resource_graph_metadata_t &m,
    std::map<std::string, vmap_val_t> &vmap,
    fetch_helper_t &fetcher,
    std::vector<fetch_helper_t> &additional_vertices)
{
    int rc = -1;
    vtx_t v = boost::graph_traits<resource_graph_t>::null_vertex ();
    if (fetcher.exclusive)
        return 0;

    if ((rc = resource_reader_jgf_t::find_vtx (g, m, vmap, fetcher, v)) != 0)
        goto error;

    return recursively_collect_vertices (g, v, additional_vertices);

error:
    return rc;
}

int resource_reader_jgf_shorthand_t::recursively_collect_vertices (
    resource_graph_t &g,
    vtx_t v,
    std::vector<fetch_helper_t> &additional_vertices)
{
    static const subsystem_t containment_sub{"containment"};
    f_out_edg_iterator_t ei, ei_end;

    if (v == boost::graph_traits<resource_graph_t>::null_vertex ()) {
        return -1;
    }

    fetch_helper_t vertex_copy;
    vertex_copy.type = g[v].type;
    vertex_copy.basename = g[v].basename;
    vertex_copy.size = g[v].size;
    vertex_copy.uniq_id = g[v].uniq_id;
    vertex_copy.rank = g[v].rank;
    vertex_copy.status = g[v].status;
    vertex_copy.id = g[v].id;
    vertex_copy.name = g[v].name;
    vertex_copy.properties = g[v].properties;
    vertex_copy.paths = g[v].paths;

    additional_vertices.push_back (vertex_copy);
    for (boost::tie (ei, ei_end) = boost::out_edges (v, g); ei != ei_end; ++ei) {
        if (g[*ei].subsystem != containment_sub)
            continue;

        if (recursively_collect_vertices (g, boost::target (*ei, g), additional_vertices) < 0) {
            return -1;
        }
    }
    return 0;
}
