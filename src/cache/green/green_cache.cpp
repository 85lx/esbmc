//
// Created by rafaelsa on 10/03/2020.
//

#include <cache/green/green_cache.h>
#include <cache/algorithms/green_normal_form.h>
#include <cache/algorithms/ssa_flattener.h>
green_cache::green_cache()
{
}

/**
 * Executes Green caching algorithms
 * @param steps SSA to apply the cache
 */
void green_cache::run(symex_target_equationt::SSA_stepst &steps)
{
  /**
   * Green algorithm consists in:
   *
   * 1. Slice the original formula
   * 2. Canonization: Puts the formula in a normal form
   * 3. Recover: Checks formula on database and slices it
   * 4. Translate: Transform the formula into SAT/SMT form
   * 5. Storage: Save formula results
   *
   * From this, I will not implement 1 and 4 since esbmc already contains
   * it's slicer and translator units.
   */

  /**
   * 2. Canonization
   *
   * It consists in:
   * a. Reorder assertions on lexical order
   */

  //lexicographical_reordering reordering(steps);
  //reordering.run();
  ssa_flattener flattener(steps);
  flattener.run();
  std::cout << "Checking collisions\n";
  flattener.dump_collisions();
  items = flattener.get_map();
}

void green_cache::succesful()
{
  std::cout << "Saving and marking as unsat\n";
  green_storage gs;
  gs.load();
  for(const auto &[key, value] : items)
  {
    gs.add(value);
  }
  gs.save();
}
