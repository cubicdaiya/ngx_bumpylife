# ngx_bumpylife

`ngx_bumpylife` sets the randomized limit of requests to be processed for each worker process.
It is inspired by [`ap-mod_bumpy_life`](https://github.com/hirose31/ap-mod_bumpy_life).

# Status

Early development phase

# Directives

## bumpylife

|Syntax |bumpylife on &#124; off |
|-------|--------------------|
|Default|off                 |
|Context|http                |

Enables or disables `ngx_bumpylife`.

## bumpylife_min

|Syntax |bumpylife number |
|-------|-----------------|
|Default|0                |
|Context|http             |

Sets the minimun value for the limit of requests to be processed.

## bumpylife_max

|Syntax |bumpylife number |
|-------|-----------------|
|Default|0                |
|Context|http             |

Sets the max value for the limit of requests to be processed.

# Behavior

If `ngx_bumpylife` is enabled, each worker process sends `SIGQUIT` to self when the number of requests to be processed arrived the limit.
The limit of requests to be processed is randomized in the `bumpylife_min` to `bumpylife_max` range.


And `ngx_bumpylife` does not work in the cases below.

 * `bumpylife_min` or `bumpylife_max` is zero.
 * `bumpylife_min` is higher than `bumpylife_max`.

# Quick Start

```nginx
http {
    bumpylife on;
    bumpylife_min 500;
    bumpylife_max 800;
    ...
}
```

# License

See [LICENSE](https://github.com/cubicdaiya/ngx_bumpylife/blob/master/LICENSE).
