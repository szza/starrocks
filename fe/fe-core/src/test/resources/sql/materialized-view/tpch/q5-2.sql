[sql]
select
    n_name,
    sum(l_extendedprice * (1 - l_discount)) as revenue
from
    customer,
    orders,
    lineitem,
    supplier,
    nation,
    region
where
        c_custkey = o_custkey
  and l_orderkey = o_orderkey
  and l_suppkey = s_suppkey
  and c_nationkey = s_nationkey
  and s_nationkey = n_nationkey
  and n_regionkey = r_regionkey
group by
    n_name
order by
    revenue desc ;
[result]
TOP-N (order by [[49: sum DESC NULLS LAST]])
    TOP-N (order by [[49: sum DESC NULLS LAST]])
        AGGREGATE ([GLOBAL] aggregate [{49: sum=sum(49: sum)}] group by [[42: n_name]] having [null]
            EXCHANGE SHUFFLE[42]
                AGGREGATE ([LOCAL] aggregate [{49: sum=sum(48: expr)}] group by [[42: n_name]] having [null]
                    SCAN (mv[lineitem_mv] columns[103: c_nationkey, 129: s_nationkey, 131: l_saleprice, 137: n_name2] predicate[129: s_nationkey = 103: c_nationkey])
[end]
