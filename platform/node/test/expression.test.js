'use strict';

var suite = require('../../../mapbox-gl-js/test/integration').expression;
var mbgl = require('../index');

var tests;

if (process.argv[1] === __filename && process.argv.length > 2) {
    tests = process.argv.slice(2);
}

suite.run('native', {tests: tests}, (fixture) => {
    const compiled = {};
    const result = {
        compiled
    };

    const expression = mbgl.Expression.parse(fixture.expression, fixture.expectExpressionType);

    if (expression instanceof mbgl.Expression) {
        compiled.result = 'success';
        compiled.isFeatureConstant = expression.isFeatureConstant();
        compiled.isZoomConstant = expression.isZoomConstant();
        compiled.type = expression.getType();

        const evaluate = fixture.inputs || [];
        const evaluateResults = [];
        for (const input of evaluate) {
            const zoom = typeof input[0].zoom === 'number' ?
                input[0].zoom : -1;

            const feature = Object.assign({
                type: 'Feature',
                properties: {},
                geometry: { type: 'Point', coordinates: [0, 0] }
            }, input[1])

            const output = expression.evaluate(zoom, feature);
            evaluateResults.push(output);
        }

        if (fixture.inputs) {
            result.outputs = evaluateResults;
        }
    } else {
        compiled.result = 'error';
        compiled.errors = expression;
    }

    return result;
});

